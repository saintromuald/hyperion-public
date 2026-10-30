// group_un.cpp -- unit control groups: recall, modify and the number-key handler.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/group_un.h"
#include "../h/CUnit.h"
#include "../h/CSprite.h"
#include "../h/minimap.h"     // cell_set_pos
#include "../h/CUnitGUI.h"    // unit_gui_reset
#include "../h/net_cmds.h"    // send_net_cmd_GroupUnits, group_recall, group_modify, sub_464360, net_cmds_init
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS
#include "../port/hud/port_hudstate.h"  // Hyperion: the HUD's data layer

#include <string.h>

// The rows this TU defines, spelled as their other declarations spell them: the
// two have to agree character for character, or the payload link sees two
// manglings of one name.
void __fastcall CenterOnSelectionGroup(BYTE nGroup);
void group_un_init();
void game_key_down_num(BYTE nDigit);

// select_units -- oracle 0x49CF80, CUnitInit.cpp's. Commits a resolved unit list
// as the local player's selection.
void __fastcall select_units(int nCount, CUnit **ppUnits, BYTE a3, int a4);

int  __fastcall select_insert(CUnit *pUnit, DWORD dwHumanID, int nCount);
void __fastcall sub_49C7D0(DWORD dwHumanID);

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// 0x527140 sgGroupUnits_g -- twelve unit ids per group, eighteen groups per
// player. group_un_init memsets 0x1B00 bytes here and 8 * 18 * 12 * 4 == 0x1B00;
// every reader forms the address as (group + player * 18) * 48 + slot * 4.
#ifndef OC_G_SGGROUPUNITS
#define OC_G_SGGROUPUNITS
#ifdef OC_DLL_MODE
#define sgGroupUnits_g (reinterpret_cast<DWORD (*)[18][SELECTION_MAX]>(OC_VA(0x527140u)))
#else
extern DWORD sgGroupUnits_g[SELECTION_HUMANS][18][SELECTION_MAX];
#endif
#endif

// 0x66CAD8 word_66CAD8 -- one stamp per hotkey group: group_un_init memsets 0x80
// bytes and 8 * 8 * 2 == 0x80, and sub_464360 walks a row backwards from
// `h * 16 + 0x66CAE8` minus two.
#ifndef OC_G_WORD_66CAD8
#define OC_G_WORD_66CAD8
#ifdef OC_DLL_MODE
#define word_66CAD8 (reinterpret_cast<WORD (*)[8]>(OC_VA(0x66CAD8u)))
#else
extern WORD word_66CAD8[8][8] OC_UNALIGNED;
#endif
#endif

// 0x463D60. Centres the view on the group the digit names.
void __fastcall CenterOnSelectionGroup(BYTE nGroup)
{
  if (nGroup >= 10)
    return;

  int nGroupIdx = nGroup;
  int nFilled = 0;

  while (nFilled < SelectionMax()) {
    if (GroupRow(g_LocalHumanID, nGroupIdx)[nFilled] == 0)
      break;
    nFilled++;
  }

  int nSumX = 0;
  int nSumY = 0;
  int nUnits = 0;

  for (int i = 0; i < SelectionMax(); i++) {
    DWORD dwId = GroupRow(g_LocalHumanID, nGroupIdx)[i];
    CUnit *pUnit;

    if (dwId == 0)
      break;
    pUnit = CUnit::FromIdAlive(dwId);
    if (pUnit == NULL)
      continue;
    if (pUnit->IsDead())
      continue;
    if (pUnit->pSprite->flags.b & 0x20)
      continue;
    if (!pUnit->IsMultiSelectable() && nFilled > 1)
      continue;
    nSumX += PosS(pUnit->pSprite->wPosX);
    nSumY += pUnit->pSprite->wPosY;
    nUnits++;
  }

  if (nUnits != 0)
    // Hyperion: the middle of the view, 320 and 200 in 1.17; of what shows beside a HUD's
    // side columns (PortViewShownWidth).
    cell_set_pos((nSumX / nUnits - PortViewShownWidth() / 2) & ~7,
                 (nSumY / nUnits - g_portView.nViewRows / 2) & ~7);
}

// 0x463EA0. Clears both group tables at game start.
void group_un_init()
{
  // Hyperion: 1.17's table; a game of Hyperion's selection clears its own as its selection
  // starts (SelectionForGame), after this
  memset(sgGroupUnits_g, 0, SELECTION_HUMANS * 18 * SELECTION_MAX * sizeof(DWORD));
  memset(word_66CAD8, 0, 0x80);
  net_cmds_init();
}

// 0x463ED0. Centres on the group, then selects it.
void sub_463ED0(BYTE nDigit)
{
  CenterOnSelectionGroup(nDigit);
  game_key_down_num(nDigit);
}

// 0x463EE0. Selects the group the digit names and tells the network about it.
void game_key_down_num(BYTE nDigit)
{
  CUnit *apUnits[SELECTION_WIDE];
  int nGroupIdx = nDigit;
  int nFilled = 0;

  while (nFilled < SelectionMax()) {
    if (GroupRow(g_LocalHumanID, nGroupIdx)[nFilled] == 0)
      break;
    nFilled++;
  }

  int nSel = 0;

  memset(apUnits, 0, sizeof(apUnits));

  CUnit *pUnit = NULL;

  for (int i = 0; i < nFilled; i++) {
    pUnit = CUnit::FromIdAlive(GroupRow(g_LocalHumanID, nGroupIdx)[i]);
    if (pUnit != NULL && !(pUnit->pSprite->flags.b & 0x20) &&
        pUnit->bPlayer == g_ActiveNationID &&
        (pUnit->IsMultiSelectable() || nFilled <= 1))
      apUnits[nSel++] = pUnit;
    else
      pUnit = NULL;
  }

  if (nSel == 0)
    return;

  if (nSel == 1 && pUnit != NULL && pUnit->bPlayer == g_ActiveNationID &&
      pUnit->IsRallyer())
    pUnit->DisplayRallyTarget();

  unit_gui_reset();
  select_units(nSel, apUnits, 1, 0);
  send_net_cmd_GroupUnits(1, nDigit, apUnits, nSel);
}

// 0x464360. Recycles the hotkey group whose stamp is oldest.
void __fastcall sub_464360()
{
  int h = g_ActiveHumanID;
  BYTE bBest = 0xFF;
  WORD wBest = 0xFFFF;
  int k = 8;

  do {
    k--;
    if (word_66CAD8[h][k] < wBest) {
      wBest = word_66CAD8[h][k];
      bBest = (BYTE)k;
    }
  } while (k != 0);

  group_modify(bBest + 10, 1);
  word_66CAD8[g_ActiveHumanID][bBest] = (WORD)dword_672658;
}

// 0x464200. Selects a stored group, compacting the dead entries out of it as it
// goes.
void __fastcall group_recall(BYTE nGroup)
{
  int nGroupIdx = nGroup;
  int h = g_ActiveHumanID;
  int nFilled = 0;

  while (nFilled < SelectionMax()) {
    if (GroupRow(h, nGroupIdx)[nFilled] == 0)
      break;
    nFilled++;
  }

  if (nFilled == 0)
    return;

  sub_49C7D0(h);

  int j = 0;
  while (j < nFilled) {
    CUnit *pUnit =
        CUnit::FromIdAlive(GroupRow(g_ActiveHumanID, nGroupIdx)[j]);

    if (pUnit != NULL && pUnit->bPlayer == g_ActiveNationID &&
        !(pUnit->pSprite->flags.b & 0x20) &&
        (pUnit->IsMultiSelectable() || nFilled <= 1)) {
      select_insert(pUnit, g_ActiveHumanID, j);
      j++;
    } else {
      nFilled--;
      GroupRow(g_ActiveHumanID, nGroupIdx)[j] =
          GroupRow(g_ActiveHumanID, nGroupIdx)[nFilled];
      GroupRow(g_ActiveHumanID, nGroupIdx)[nFilled] = 0;
    }
  }

  if (nGroup >= 10)
    word_66CAD8[g_ActiveHumanID][nGroupIdx - 10] = (WORD)dword_672658;
  else
    sub_464360();
  PortHudGroup(nGroup, false);  // Hyperion: group.recall (port_hudstate.cpp)
}

// 0x464030. Finds the most recently stamped hotkey group holding the unit and
// selects it. Returns whether one was found.
//
// The identical arms on nSel below are merged after register allocation; the
// extra nSel reference they count gives the first loop the oracle's registers.
int __fastcall sub_464030(int nUnitId)
{
  CUnit *apUnits[SELECTION_WIDE];
  int h = g_LocalHumanID;
  int nBest = -1;
  int k = 8;

  do {
    k--;
    for (int s = 0; s < SelectionMax(); s++) {
      DWORD dwId = GroupRow(h, k + 10)[s];

      if (dwId != 0 && dwId == (DWORD)nUnitId &&
          word_66CAD8[h][k] > word_66CAD8[g_LocalHumanID][nBest])
        nBest = k;
    }
  } while (k != 0);

  if (nBest > -1) {
    BYTE bGroup = (BYTE)(nBest + 10);
    int nGroupIdx = bGroup;
    int nFilled = 0;

    while (nFilled < SelectionMax()) {
      if (GroupRow(g_LocalHumanID, nGroupIdx)[nFilled] == 0)
        break;
      nFilled++;
    }

    int nSel = 0;

    memset(apUnits, 0, sizeof(apUnits));

    CUnit *pUnit = NULL;

    for (int i = 0; i < nFilled; i++) {
      pUnit = CUnit::FromIdAlive(GroupRow(g_LocalHumanID, nGroupIdx)[i]);
      if (pUnit != NULL && !(pUnit->pSprite->flags.b & 0x20) &&
          pUnit->bPlayer == g_ActiveNationID &&
          (pUnit->IsMultiSelectable() || nFilled <= 1))
        apUnits[nSel++] = pUnit;
      else if (nSel)
        pUnit = NULL;
      else
        pUnit = NULL;
    }

    if (nSel != 0) {
      if (nSel == 1 && pUnit != NULL && pUnit->bPlayer == g_ActiveNationID &&
          pUnit->IsRallyer())
        pUnit->DisplayRallyTarget();

      unit_gui_reset();
      select_units(nSel, apUnits, 1, 0);
      send_net_cmd_GroupUnits(1, bGroup, apUnits, nSel);
    }
    return 1;
  }
  return 0;
}

// 0x4643C0. Stores the current selection into a group, either replacing it or
// appending the units it does not already hold.
void __fastcall group_modify(BYTE nGroup, BOOL bAdd)
{
  PortHudGroup(nGroup, true);  // Hyperion: group.set (port_hudstate.cpp)
  int nGroupIdx;
  int nFilled;

  if (bAdd == 0) {
    CUnit *pFirst;

    nGroupIdx = nGroup;
    pFirst = CUnit::FromIdAlive(GroupRow(g_ActiveHumanID, nGroupIdx)[0]);
    if (pFirst != NULL && !pFirst->IsMultiSelectable())
      return;

    nFilled = 0;
    while (nFilled < SelectionMax()) {
      if (GroupRow(g_ActiveHumanID, nGroupIdx)[nFilled] == 0)
        break;
      nFilled++;
    }
  } else {
    nGroupIdx = nGroup;
    memset(GroupRow(g_ActiveHumanID, nGroupIdx), 0, SelectionMax() * sizeof(DWORD));
    nFilled = 0;
  }

  for (int i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = SelectedRow(g_ActiveHumanID)[i];
    DWORD dwId;

    if (pUnit == NULL)
      return;
    if (pUnit->bPlayer != g_ActiveNationID)
      return;

    dwId = pUnit->GetUnitID();
    if (dwId == 0)
      continue;

    if (bAdd == 0) {
      int j;

      for (j = 0; j < nFilled; j++) {
        if (GroupRow(g_ActiveHumanID, nGroupIdx)[j] == dwId)
          goto next;
      }
      if (nFilled != 0 &&
          !SelectedRow(g_ActiveHumanID)[i]->IsMultiSelectable())
        continue;
    }

    GroupRow(g_ActiveHumanID, nGroupIdx)[nFilled] = dwId;
    nFilled++;
    if (nFilled >= SelectionMax())
      return;
  next:;
  }
}
