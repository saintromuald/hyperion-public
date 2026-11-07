// smartcast_test.cpp -- focused checks for select.cpp's stateless smart-cast ranking.
//
// Include the implementation so the test calls the same pure score helpers as SelectSmartCaster.
// Build with function/data sections and --gc-sections: the selection layer's unrelated
// functions then need none of the game's objects at link time.
#include <stdio.h>

#include "../../src117/lang/select.cpp"

// New games record generalized rotation. The legacy check below clears it again.
unsigned g_hxRuleOptions = HX_OPT_SMART_CAST_ROTATION;

struct Candidate {
  bool bHasEnergy;
  bool bCommitted;
  long long qDistance;
};

static int Pick(const Candidate *pCandidates, int nCount, bool bDistribute)
{
  int nBest = -1;
  SmartCastScore best = {0, 0};
  for (int i = 0; i < nCount; ++i) {
    const SmartCastScore score = SmartCastScoreFor(
        pCandidates[i].qDistance, pCandidates[i].bHasEnergy,
        pCandidates[i].bCommitted, bDistribute);
    if (nBest < 0 || SmartCastScoreBetter(score, best)) {
      nBest = i;
      best = score;
    }
  }
  return nBest;
}

static int Check(int nGot, int nWant, const char *pszWhat)
{
  if (nGot == nWant)
    return 0;
  fprintf(stderr, "%s: got caster %d, expected %d\n", pszWhat, nGot, nWant);
  return 1;
}

int main()
{
  int nFailed = 0;
  Candidate casters[] = {
      {true, false, 1},
      {true, false, 4},
      {true, false, 9},
  };

  const bool bImmediateConsume = SmartCastDistributes(0x10, 0, 0);
  const bool bImmediateEnsnare = SmartCastDistributes(0x11, 75 << 8, 0);
  const bool bQueuedConsume = SmartCastDistributes(0x10, 0, 1);
  const bool bQueuedEnsnare = SmartCastDistributes(0x11, 75 << 8, 1);
  const bool bZeroCostOther = SmartCastDistributes(0x11, 0, 0);

  // The nearest caster commits, then the next nearest, until every caster has one order.
  for (int i = 0; i < 3; ++i) {
    nFailed += Check(Pick(casters, 3, bImmediateConsume), i, "immediate Consume rotation");
    casters[i].bCommitted = true;
  }
  nFailed += Check(Pick(casters, 3, bImmediateConsume), 0, "all-committed Consume fallback");

  // An energy-costing spell such as Queen Ensnare rotates in exactly the same way.
  for (int i = 0; i < 3; ++i) {
    casters[i].bCommitted = false;
  }
  for (int i = 0; i < 3; ++i) {
    nFailed += Check(Pick(casters, 3, bImmediateEnsnare), i, "immediate Ensnare rotation");
    casters[i].bCommitted = true;
  }
  nFailed += Check(Pick(casters, 3, bImmediateEnsnare), 0, "all-committed Ensnare fallback");
  nFailed += Check(SmartCastDistributes(0x0C, 0, 0), 1, "free Infestation rotation");

  // Queued commands and unrelated zero-cost orders preserve nearest-caster smart
  // cast: caster 1 is uncommitted, but committed caster 0 remains the pick.
  casters[1].bCommitted = false;
  nFailed += Check(Pick(casters, 3, bQueuedConsume), 0, "queued Consume");
  nFailed += Check(Pick(casters, 3, bQueuedEnsnare), 0, "queued Ensnare");
  nFailed += Check(Pick(casters, 3, bZeroCostOther), 0, "zero-cost non-Consume order");

  // An uncommitted caster is tried before a committed one, even when its refusal is the
  // result; with everybody committed, the old energy-first fallback applies.
  casters[0].bCommitted = true;
  casters[0].bHasEnergy = true;
  casters[1].bCommitted = true;
  casters[1].bHasEnergy = true;
  casters[2].bCommitted = false;
  casters[2].bHasEnergy = false;
  nFailed += Check(Pick(casters, 3, bImmediateEnsnare), 2, "uncommitted low-energy caster");
  casters[2].bCommitted = true;
  nFailed += Check(Pick(casters, 3, bImmediateEnsnare), 0, "committed energy-first fallback");

  // Without distribution this is exactly the old energy-first, then nearest, behavior.
  Candidate energy[] = {
      {false, false, 1},
      {true, false, 100},
      {true, false, 400},
  };
  nFailed += Check(Pick(energy, 3, false), 1, "energy before distance");
  energy[1].bHasEnergy = false;
  energy[2].bHasEnergy = false;
  nFailed += Check(Pick(energy, 3, false), 0, "nearest refusal without energy");

  // An older record has no generalized-rotation option: Ensnare keeps its old nearest-caster
  // choice, while the Consume fix that record already knew continues to rotate.
  g_hxRuleOptions = 0;
  nFailed += Check(SmartCastDistributes(0x11, 75 << 8, 0), 0, "legacy Ensnare rule");
  nFailed += Check(SmartCastDistributes(0x0C, 0, 0), 0, "legacy Infestation rule");
  nFailed += Check(SmartCastDistributes(0x10, 0, 0), 1, "legacy Consume rule");
  g_hxRuleOptions = HX_OPT_SMART_CAST_ROTATION;

  if (nFailed == 0)
    puts("smart-cast score checks passed");
  return nFailed != 0;
}
