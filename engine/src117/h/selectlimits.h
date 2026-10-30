// selectlimits.h -- the selection's size, and Hyperion's wider selection.
//
// 1.17 selects up to 12 units and writes the 12 again in every array that holds a
// selection and every loop over one: gSelected (a row for each of the 8 humans),
// gpNewUnits, gpStatUnits, sgpNetSelected_g, sgSavedSelected_g, the groups
// (sgGroupUnits_g) and the health bars (sgHpbarImages). The size travels with the game:
// the selection's commands carry at most this many units (0x09 to 0x0B, and 0x89 to 0x8B
// with wide unit ids), a hotkey's command (0x13) selects a group of as many, replays
// record those commands and saves hold every human's selection.
//
// A game by Hyperion's rules that records HX_OPT_SELECTION (hxrules.h) selects up to 255:
// its selections, groups and the console's units are rows of the port's own
// (port_selwide.cpp), its selection is sent in chunks (0x8C, net_cmds.h) and its save
// keeps the rows (HXST, port_hxsave.cpp). 1.17's arrays stay as they are, empty in such a
// game, so 1.17's saves, the EUD twins and every other game keep 1.17's layout. The game's
// code reaches a selection through the accessors below, which give 1.17's arrays and 12 in
// every other game.
#ifndef SELECTLIMITS_H
#define SELECTLIMITS_H

#include "platform.h"

#define SELECTION_MAX 12     // the units one selection holds in 1.17
#define SELECTION_WIDE 255   // and in a game of HX_OPT_SELECTION
#define SELECTION_HUMANS 8   // the humans gSelected keeps a row for
#define SELECTION_GROUPS 18  // a human's groups: the ten hotkeys, then eight recent selections
#define SELECTION_PAGE 12    // the units the console's selection shows at a time

class CUnit;

// The arrays the game uses now, set as each game starts (SelectionForGame).
extern int g_nSelectionMax;         // 12, or 255 in a game of HX_OPT_SELECTION
extern CUnit **g_ppSelected;        // gSelected: a row of g_nSelectionMax for each human
extern CUnit **g_ppNewUnits;        // gpNewUnits: the local selection, as last committed
extern CUnit **g_ppStatUnits;       // gpStatUnits: the units the console shows
extern CUnit **g_ppNetSelected;     // sgpNetSelected_g: the selection the network was told
extern CUnit **g_ppSavedSelected;   // sgSavedSelected_g: the selection a load restores
extern DWORD *g_pdwGroups;          // sgGroupUnits_g: unit ids, SELECTION_GROUPS rows a human

static inline int SelectionMax() { return g_nSelectionMax; }
static inline bool SelectionWide() { return g_nSelectionMax > SELECTION_MAX; }
static inline CUnit **SelectedRow(int nHuman) { return g_ppSelected + nHuman * g_nSelectionMax; }
static inline CUnit **NewUnits() { return g_ppNewUnits; }
static inline CUnit **StatUnits() { return g_ppStatUnits; }
static inline CUnit **NetSelected() { return g_ppNetSelected; }
static inline CUnit **SavedSelected() { return g_ppSavedSelected; }
static inline DWORD *GroupRow(int nHuman, int nGroup) {
  return g_pdwGroups + (nHuman * SELECTION_GROUPS + nGroup) * g_nSelectionMax;
}

// As a game starts, ahead of the selection's own start (select_init): 1.17's arrays and 12,
// or the port's rows and 255 when the game records HX_OPT_SELECTION.
void SelectionForGame();
// The local selection's page the console shows, 12 units from its first (statdraw.cpp,
// statselect.cpp); 0 outside a game of HX_OPT_SELECTION. Presentation only.
int SelectionPageFirst();
void SelectionPageSet(int nFirst);

// The console's subgroups in a game of Hyperion's selection, as StarCraft II's: the
// kinds of unit selected, by their rank, the console's units sorted into them
// (SelectionSubgroupsSort, as the console takes a new selection; a selection a unit left keeps
// its subgroup, a new one starts at the first). The console reports on the subgroup's first
// unit, shows its card and the page that holds it. SelectionSubgroupStep moves to the next
// (1) or the last (-1), the subgroup key's work. Presentation only: a command from the card
// goes to every selected unit that can carry it out, as in 1.17.
void SelectionSubgroupsSort();
void SelectionSubgroupStep(int nStep);
int SelectionSubgroup();
int SelectionSubgroups();  // the kinds, as last sorted; 1 outside a game of Hyperion's selection
// Whether the console holds a unit of another kind than the subgroup's, in a game of
// Hyperion's selection. The card's buttons that are one kind's own form of a shared command
// (order.cpp: the medic's heal-move, the carrier's and the reaver's attack, move and stop) then
// send the shared command, as 1.17's card for a mixed selection does, and select_target gives
// each unit its own form of it; the medic's heal-move alone, the marines would refuse it.
bool SelectionMixed();

// A save of such a game (port_hxsave.cpp's HXST): every human's row and groups, as unit ids.
int SelectionSave(void *pFile);
int SelectionLoad(void *pFile);

#endif
