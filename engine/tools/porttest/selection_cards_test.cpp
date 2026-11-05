// selection_cards_test.cpp -- focused checks for one selection card per unit type.
//
// Include the implementation so the test uses the same grouping and totals as both consoles.
// Function/data sections and --gc-sections leave the unrelated selection systems unlinked.
#include <stdio.h>

#include "../../src117/port/gameplay/port_selwide.cpp"

CUnit *gpStatUnits[SELECTION_MAX] = {};
bool PortSnapStatic(void *, unsigned long) { return true; }

static int Check(bool bOK, const char *pszWhat)
{
  if (bOK)
    return 0;
  fprintf(stderr, "%s\n", pszWhat);
  return 1;
}

int main()
{
  int nFailed = 0;
  CUnit units[SELECTION_WIDE] = {};
  CUnit *selected[SELECTION_WIDE] = {};
  g_nSelectionMax = SELECTION_WIDE;
  g_ppStatUnits = selected;

  // Twelve Marines and three Medics are two cards, not one card and not fifteen cards.
  for (int i = 0; i < 15; ++i) {
    units[i].wType = i < 12 ? 0x00 : 0x01;
    selected[i] = &units[i];
  }
  SelectionCard cards[SELECTION_WIDE];
  int nCards = SelectionCards(cards, 0, SELECTION_WIDE);
  nFailed += Check(nCards == 2, "mixed selection must have two type cards");
  nFailed += Check(cards[0].pUnit == selected[0] && cards[0].nUnits == 12,
                   "Marine card must name its first unit and count twelve");
  nFailed += Check(cards[1].pUnit == selected[12] && cards[1].nUnits == 3,
                   "Medic card must name its first unit and count three");
  nFailed += Check(SelectionCardFor(selected[14]) == 1, "Medic must find the Medic card");

  // Thirteen types cross a console page. Counts include units on both pages and no type is
  // repeated merely because its representative or fellow units are past the first page.
  for (int i = 0; i < 26; ++i) {
    units[i].wType = static_cast<WORD>(i / 2);
    selected[i] = &units[i];
  }
  for (int i = 26; i < SELECTION_WIDE; ++i)
    selected[i] = NULL;
  nCards = SelectionCards(cards, 0, SELECTION_WIDE);
  nFailed += Check(nCards == 13, "thirteen types must produce thirteen cards");
  SelectionCard first[SELECTION_PAGE];
  SelectionCard last[1];
  nFailed += Check(SelectionCards(first, 0, SELECTION_PAGE) == 13,
                   "asking a page must still return the total card count");
  SelectionCards(last, SELECTION_PAGE, 1);
  for (int i = 0; i < SELECTION_PAGE; ++i)
    nFailed += Check(first[i].nUnits == 2, "first-page card must count all units of its type");
  nFailed += Check(last[0].pUnit == selected[24] && last[0].nUnits == 2,
                   "second-page card must be the thirteenth type with its full count");
  nFailed += Check(SelectionCardFor(selected[25]) == SELECTION_PAGE,
                   "a unit on the second page must find its global card index");

  if (nFailed == 0)
    puts("selection type-card checks passed");
  return nFailed != 0;
}
