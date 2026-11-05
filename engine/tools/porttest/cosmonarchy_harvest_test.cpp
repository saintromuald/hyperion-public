// cosmonarchy_harvest_test.cpp -- focused checks for Cosmonarchy's per-trip yields.
//
// Include the implementation to test its public harvest rule directly. Build with
// function/data sections and --gc-sections so the unrelated mod systems need no game objects.
#include <stdio.h>

#include "../../src117/port/gameplay/port_cosmonarchy.cpp"

// CosmoHarvestAmount's gas-tier branch resolves type names even though these mineral cases
// do not take it. No gas type lookup is needed for the direct gas-yield checks below.
int ModTypeByName(const char *, const char *)
{
  return -1;
}

static int CheckAmount(WORD wType, WORD wRemaining, int nWant)
{
  CUnit node = {};
  node.wType = wType;
  node.wResourceAmount = wRemaining;
  const int nGot = CosmoHarvestAmount(&node);
  if (nGot == nWant)
    return 0;
  fprintf(stderr, "mineral type %u, remaining %u: got %d, expected %d\n",
          static_cast<unsigned>(wType), static_cast<unsigned>(wRemaining), nGot, nWant);
  return 1;
}

int main()
{
  int nFailed = 0;
  for (WORD wType = 0xB0; wType <= 0xB2; ++wType) {
    nFailed += CheckAmount(wType, 1500, 5);
    nFailed += CheckAmount(wType, 7, 5);
    nFailed += CheckAmount(wType, 3, 5);
    nFailed += CheckAmount(wType, 2, 2);
    nFailed += CheckAmount(wType, 1, 2);
    nFailed += CheckAmount(wType, 0, 2);
  }

  static const int anGasWant[] = {3, 5, 7, 9};
  for (int i = 0; i < 4; ++i) {
    const int nGot = CosmoYield(i + 2);
    if (nGot != anGasWant[i]) {
      fprintf(stderr, "gas yield %d: got %d, expected %d\n", i, nGot, anGasWant[i]);
      ++nFailed;
    }
  }

  if (nFailed == 0)
    puts("Cosmonarchy harvest-yield checks passed");
  return nFailed != 0;
}
