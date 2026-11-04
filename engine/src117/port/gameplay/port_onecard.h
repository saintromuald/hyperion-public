// port_onecard.h -- the console's one-card selection display (the user, 2026-11-04).
#ifndef PORT_ONECARD_H
#define PORT_ONECARD_H

#include "../../h/rect16.h"

// The active unit's one card in the console's selection strip: the selection's count to
// draw on it, and the card's rect in the console's design pixels. statdraw_draw_multi sets
// them when it shows one card (a selection of more than one); the overlay
// (port_mineinfo.cpp) draws the count on the card.
extern int g_nOneCardCount;
extern rect16_t g_rcOneCard;

#endif
