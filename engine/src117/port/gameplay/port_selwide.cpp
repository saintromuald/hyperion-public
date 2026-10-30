// port_selwide.cpp -- Hyperion's selection (stage S; h/selectlimits.h): a game by
// Hyperion's rules that records HX_OPT_SELECTION selects up to 255 units, in rows of the
// port's own, and every other game in 1.17's arrays of 12.
#include <windows.h>
#include <string.h>
#include <algorithm>
#include <vector>

#include "CUnit.h"
#include "CImage.h"
#include "oc_data.h"
#include "../../h/hxrules.h"
#include "../../h/selectlimits.h"
#include "../../h/saveload.h"  // pfread, pfwrite, compress, decompress
#include "../replays/port_snap.h"  // PORT_SNAP_KEEP
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

namespace {

// The rows of a game of HX_OPT_SELECTION, in 1.17's shapes at 255.
CUnit *g_apSelected[SELECTION_HUMANS * SELECTION_WIDE];
CUnit *g_apNewUnits[SELECTION_WIDE];
CUnit *g_apStatUnits[SELECTION_WIDE];
CUnit *g_apNetSelected[SELECTION_WIDE];
CUnit *g_apSavedSelected[SELECTION_WIDE];
DWORD g_adwGroups[SELECTION_HUMANS * SELECTION_GROUPS * SELECTION_WIDE];
int g_nPageFirst;

}  // namespace

// the trainer's snapshots hold them (port_snap.cpp)
PORT_SNAP_KEEP(g_apSelected);
PORT_SNAP_KEEP(g_apNewUnits);
PORT_SNAP_KEEP(g_apStatUnits);
PORT_SNAP_KEEP(g_apNetSelected);
PORT_SNAP_KEEP(g_apSavedSelected);
PORT_SNAP_KEEP(g_adwGroups);

int g_nSelectionMax = SELECTION_MAX;
CUnit **g_ppSelected = gSelected;
CUnit **g_ppNewUnits = gpNewUnits;
CUnit **g_ppStatUnits = gpStatUnits;
CUnit **g_ppNetSelected = sgpNetSelected_g;
CUnit **g_ppSavedSelected = sgSavedSelected_g;
DWORD *g_pdwGroups = &sgGroupUnits_g[0][0][0];

void SelectionForGame() {
  // An EUD map reads 1.17's arrays (port_eud.cpp), so its game keeps them; a new game on one
  // records no HX_OPT_SELECTION (EudMapTriggersRead).
  const bool bWide = HxRules() && (g_hxRuleOptions & HX_OPT_SELECTION) != 0;
  g_nPageFirst = 0;
  if (!bWide) {
    g_nSelectionMax = SELECTION_MAX;
    g_ppSelected = gSelected;
    g_ppNewUnits = gpNewUnits;
    g_ppStatUnits = gpStatUnits;
    g_ppNetSelected = sgpNetSelected_g;
    g_ppSavedSelected = sgSavedSelected_g;
    g_pdwGroups = &sgGroupUnits_g[0][0][0];
    return;
  }
  memset(g_apSelected, 0, sizeof g_apSelected);
  memset(g_apNewUnits, 0, sizeof g_apNewUnits);
  memset(g_apStatUnits, 0, sizeof g_apStatUnits);
  memset(g_apNetSelected, 0, sizeof g_apNetSelected);
  memset(g_apSavedSelected, 0, sizeof g_apSavedSelected);
  memset(g_adwGroups, 0, sizeof g_adwGroups);
  g_nSelectionMax = SELECTION_WIDE;
  g_ppSelected = g_apSelected;
  g_ppNewUnits = g_apNewUnits;
  g_ppStatUnits = g_apStatUnits;
  g_ppNetSelected = g_apNetSelected;
  g_ppSavedSelected = g_apSavedSelected;
  g_pdwGroups = g_adwGroups;
  // the health bars and circles of as many units (CImage.cpp); none is out yet
  CImage::InitSelectionImagesWide();
}

int SelectionPageFirst() {
  return SelectionWide() ? g_nPageFirst : 0;
}

void SelectionPageSet(int nFirst) {
  g_nPageFirst = nFirst < 0 ? 0 : nFirst - nFirst % SELECTION_PAGE;
}

int SelectionSave(void *pFile) {
  std::vector<DWORD> ids(SELECTION_HUMANS * SELECTION_WIDE);
  for (size_t i = 0; i < ids.size(); i++)
    ids[i] = g_apSelected[i] != NULL ? g_apSelected[i]->GetUnitID() : 0;
  return compress(ids.data(), static_cast<int>(ids.size() * 4), pFile)
      && compress(g_adwGroups, static_cast<int>(sizeof g_adwGroups), pFile);
}

int SelectionLoad(void *pFile) {
  std::vector<DWORD> ids(SELECTION_HUMANS * SELECTION_WIDE);
  if (!decompress(ids.data(), static_cast<int>(ids.size() * 4), pFile)
      || !decompress(g_adwGroups, static_cast<int>(sizeof g_adwGroups), pFile))
    return 0;
  for (size_t i = 0; i < ids.size(); i++)
    g_apSelected[i] = ids[i] != 0 ? CUnit::FromIdAlive(ids[i]) : NULL;
  return 1;
}

// ---- the console's subgroups --------------------------------------------------------

extern BYTE gubUnitBaseRank[228] OC_UNALIGNED;  // a unit type's rank, as select.cpp weighs it
void sub_4A18D0();                 // statcmd.cpp: the card again for gpStatUnit
void __fastcall status_set_update();

namespace {

WORD g_wSubgroupType = 0xFFFF;     // the kind of unit the console's subgroup is
int g_nSubgroup;                   // its place among the kinds
int g_nKinds;                      // the kinds
std::vector<CUnit *> g_lastSelection;  // the console's selection as it was last sorted

int Rank(WORD wType) {
  return wType < 228 ? gubUnitBaseRank[wType] : 0;
}

// The console's units and their kinds, in the console's order.
int ConsoleUnits(std::vector<WORD> *pTypes) {
  CUnit **pp = StatUnits();
  int n = 0;
  while (n < SelectionMax() && pp[n] != NULL)
    n++;
  pTypes->clear();
  for (int i = 0; i < n; i++)
    if (std::find(pTypes->begin(), pTypes->end(), pp[i]->wType) == pTypes->end())
      pTypes->push_back(pp[i]->wType);
  return n;
}

// The subgroup k of the kinds: the console reports on its first unit and shows its page.
void Show(const std::vector<WORD> &types, int n, int k) {
  g_nSubgroup = k;
  g_nKinds = static_cast<int>(types.size());
  g_wSubgroupType = types[k];
  CUnit **pp = StatUnits();
  for (int i = 0; i < n; i++) {
    if (pp[i]->wType == g_wSubgroupType) {
      gpStatUnit = pp[i];
      g_nPageFirst = i - i % SELECTION_PAGE;
      return;
    }
  }
}

}  // namespace

void SelectionSubgroupsSort() {
  if (!SelectionWide())
    return;
  std::vector<WORD> types;
  const int n = ConsoleUnits(&types);
  if (n < 2) {
    g_nSubgroup = 0;
    g_nKinds = n;
    g_wSubgroupType = n != 0 ? StatUnits()[0]->wType : 0xFFFF;
    g_nPageFirst = 0;
    g_lastSelection.assign(StatUnits(), StatUnits() + n);
    return;
  }
  // the kinds by their rank, highest first, then as they were selected; the units by kind
  std::stable_sort(types.begin(), types.end(), [](WORD a, WORD b) { return Rank(a) > Rank(b); });
  std::vector<CUnit *> sorted;
  sorted.reserve(n);
  for (WORD t : types)
    for (int i = 0; i < n; i++)
      if (StatUnits()[i]->wType == t)
        sorted.push_back(StatUnits()[i]);
  std::copy(sorted.begin(), sorted.end(), StatUnits());
  // a selection narrowed (a unit gone from it) keeps its subgroup; a new one starts at the first
  bool bNarrowed = true;
  for (CUnit *p : sorted)
    bNarrowed = bNarrowed && std::find(g_lastSelection.begin(), g_lastSelection.end(), p) != g_lastSelection.end();
  int k = 0;
  if (bNarrowed) {
    const auto at = std::find(types.begin(), types.end(), g_wSubgroupType);
    if (at != types.end())
      k = static_cast<int>(at - types.begin());
  }
  Show(types, n, k);
  g_lastSelection = sorted;
}

void SelectionSubgroupStep(int nStep) {
  if (!SelectionWide())
    return;
  std::vector<WORD> types;
  const int n = ConsoleUnits(&types);
  if (types.size() < 2)
    return;
  const int nTypes = static_cast<int>(types.size());
  Show(types, n, ((g_nSubgroup + nStep) % nTypes + nTypes) % nTypes);
  // the card, the portrait and the page again
  sub_4A18D0();
  status_set_update();
}

int SelectionSubgroup() {
  return SelectionWide() ? g_nSubgroup : 0;
}

int SelectionSubgroups() {
  return SelectionWide() ? g_nKinds : 1;
}

bool SelectionMixed() {
  if (!SelectionWide() || gpStatUnit == NULL)
    return false;
  CUnit **pp = StatUnits();
  for (int i = 0; i < SelectionMax(); i++)
    if (pp[i] != NULL && pp[i]->wType != gpStatUnit->wType)
      return true;
  return false;
}
