// port_onecard.h -- the classic console's type-card labels (the file keeps its original name).
#ifndef PORT_ONECARD_H
#define PORT_ONECARD_H

#include "../../h/rect16.h"
#include "../../h/selectlimits.h"  // SELECTION_PAGE

// The visible type cards in the console's selection strip: each card's total unit count and
// its rect in the console's design pixels. statdraw_draw_multi fills these and the overlay
// (port_mineinfo.cpp) draws the counts. Counts cover the whole selection, not just the page.
struct SelectionCardLabel {
  rect16_t rc;
  int nUnits;
};
extern int g_nSelectionCardLabels;
extern SelectionCardLabel g_aSelectionCardLabels[SELECTION_PAGE];

#endif
