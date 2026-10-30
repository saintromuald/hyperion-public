// port_pathstats.h -- counters of the pathing for the live channel's pathstats command and
// tools/pathbench.py. They count and
// time and are never read by the game, so a game plays the same with them.
#ifndef PORT_PATHSTATS_H
#define PORT_PATHSTATS_H

struct PathStats {
  double fBuildMs;      // the last build of the map's regions and contours (sub_490D80)
  unsigned nArea;       // region searches (sub_491300)
  unsigned nAreaCapped; // ... that filled the queue of 125 or the 350 nodes
  unsigned nAreaShort;  // ... that did not reach the goal and took the closest region
  unsigned nLocal;      // local searches (sub_491F50)
  unsigned nLocalCapped;// ... that filled the queue of 150 or the 250 entries
  unsigned nLocalShort; // ... that ended on the closest tile instead of the goal
  unsigned nLocalNodes; // entries the local searches used, in all
};

extern PathStats g_pathStats;

void PathStatsBuildBegin();
void PathStatsBuildEnd();

#endif
