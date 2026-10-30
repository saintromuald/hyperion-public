// port_front_mutators.h -- the skirmish set-up's Mutators tab, and the word on the mutators at
// the Game Options tab's foot (the mutators themselves are
// ../gameplay/port_mutators.h).
//
// The tab is a list of the mutators by group and, beside it, the briefing of one: what it does
// in a sentence, a still picture of Classic beside this game in the game's wireframes, its
// choices as chips, and what the choice does. Pointing at a row briefs it until the pointer
// leaves the list; pointing at a chip shows that choice in the picture and the line before it is
// taken. A choice harder than Classic is marked in a dark red where any other is Hyperion's gold.
#pragma once
#include <string>

#include "../gameplay/port_session.h"  // MutatorSet

namespace FrontKit {

struct MapPicture;

// What the tab reads from the set-up: the set, whether this player chooses it (a guest sees the
// host's), the map, the game's speed's name, and whether the map's own start units stand in for
// the start workers (Use Map Settings).
struct MutatorsSetUp {
  MutatorSet *pSet;
  bool bMine;
  const MapPicture *pMap;
  const char *pszMapTitle;
  const char *pszSpeed;
  bool bMapUnits;
};

// The tabs over the set-up's right column at x, y, w wide in SC2's units: Game Options (O) and
// Mutators (U), which counts the mutators on, and the set in a word at the strip's right. The
// tab clicked, 0 or 1, or -1. pszFirst and pszFirstTip name the first tab another way (the
// benchmark's options).
int MutatorsViewTabs(float x, float y, float w, int nView, const MutatorSet &m, const char *pszFirst = NULL,
                     const char *pszFirstTip = NULL);
// The Mutators tab's box at x, y, w by h; true when the set changed.
bool MutatorsTab(float x, float y, float w, float h, const MutatorsSetUp &s);
// The Game Options tab's word on them, at the options' foot (laid out in the open box, as wide
// as it): which are on, and the way to them. True when its button asks for the Mutators tab.
bool MutatorsSummary(const MutatorsSetUp &s);
// The tab's keys while it is up: Up and Down through the list, Left and Right through the
// chosen one's choices. True when it took one; *pbChanged when the set changed.
bool MutatorsTabKeys(const MutatorsSetUp &s, bool *pbChanged);
// Brief the nAt-th in the list, in Up and Down's order (a check without a person).
void MutatorsTabBrief(int nAt);

}  // namespace FrontKit
