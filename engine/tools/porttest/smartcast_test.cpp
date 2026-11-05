// smartcast_test.cpp -- focused checks for select.cpp's stateless smart-cast ranking.
//
// Include the implementation so the test calls the same pure score helpers as SmartCaster.
// Build with function/data sections and --gc-sections: the selection layer's unrelated
// functions then need none of the game's objects at link time.
#include <stdio.h>

#include "../../src117/lang/select.cpp"

struct Candidate {
  bool bHasEnergy;
  bool bCommitted;
  long long qDistance;
};

static int Pick(const Candidate *pCandidates, int nCount, bool bDistributeConsume)
{
  int nBest = -1;
  SmartCastScore best = {0, 0};
  for (int i = 0; i < nCount; ++i) {
    const SmartCastScore score = SmartCastScoreFor(
        pCandidates[i].qDistance, pCandidates[i].bHasEnergy,
        pCandidates[i].bCommitted, bDistributeConsume);
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

  // The nearest caster commits, then the next nearest, until every caster has one order.
  for (int i = 0; i < 3; ++i) {
    nFailed += Check(Pick(casters, 3, true), i, "immediate Consume rotation");
    casters[i].bCommitted = true;
  }
  nFailed += Check(Pick(casters, 3, true), 0, "all-committed Consume fallback");

  // Other spells and queued Consume preserve nearest-caster smart cast even when it repeats.
  nFailed += Check(Pick(casters, 3, false), 0, "other smart-cast spell");
  nFailed += Check(Pick(casters, 3, false), 0, "queued Consume");

  // An uncommitted caster is tried before a committed one, even when its refusal is the
  // result; with everybody committed, the old energy-first fallback applies.
  casters[0].bCommitted = true;
  casters[0].bHasEnergy = true;
  casters[1].bCommitted = true;
  casters[1].bHasEnergy = true;
  casters[2].bCommitted = false;
  casters[2].bHasEnergy = false;
  nFailed += Check(Pick(casters, 3, true), 2, "uncommitted low-energy caster");
  casters[2].bCommitted = true;
  nFailed += Check(Pick(casters, 3, true), 0, "committed energy-first fallback");

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

  if (nFailed == 0)
    puts("smart-cast score checks passed");
  return nFailed != 0;
}
