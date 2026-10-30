// scroll.h -- the map-scroll input layer in src117/lang/scroll.cpp (oracle
// 0x49B7C0-0x49C140). Seven of this TU's rows are declared elsewhere already
// and stay there: IsAutoScroll in minimap.h, scroll_free, scroll_init_game,
// scroll_reset, DrawStars and DrawStarsDirty in gamemap.h, scroll_update in
// gameloop.h.

#ifndef OC117_SCROLL_H
#define OC117_SCROLL_H

#include "platform.h"

// Both register arguments are written as dwords, `mov dword ptr [edx], 0h`.
// ECX takes the horizontal shift: scroll_update reads it back into the slot it
// then tests for cell_scroll_lft / cell_scroll_rgt.
void scroll_get_shift(int *pnShiftX, int *pnShiftY);

// `ret 4h`. The stack word is called with nothing pushed, so it is nullary.
void scroll_start_auto(int nX, int nY, void (*pfnDone)());

void scroll_end_auto();

// ECX is the ramp slot, read and written as a dword. The result is the literal
// 6, the fastest row of the ramp table.
int scroll_get_speed(int *pnRamp);

// Hyperion: the auto-scroll a centre-view trigger started, its target in *pnX and *pnY;
// false when none runs. And whether the game's window counts as active, which the scroll needs
// to move. For the live channel's status (port_control.cpp).
bool ScrollAutoTarget(int *pnX, int *pnY);
bool ScrollWindowActive(void);

#endif  // OC117_SCROLL_H
