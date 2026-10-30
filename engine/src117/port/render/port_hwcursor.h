// port_hwcursor.h -- Settings' "Hardware cursor": the game's cursor drawn by the system over the
// window, so it moves with the mouse between the game's frames (port_hwcursor.cpp,
// docs/modules/render.md). And the cursor over a HUD design, drawn above it.
#pragma once

#include <vector>

struct SDL_Color;
struct SDL_Cursor;
struct FRAME;

// The setting ("hardwareCursor", off unless set on).
void PortHwCursorSet(bool bOn);
bool PortHwCursorOn(void);
// The system cursor for the game's cursor frame as it now is, each game pixel fScale client
// pixels across, in the screen's colours; NULL while the game shows none. Each one is kept
// until the scale or a colour it uses changes, and freed once the system no longer shows it.
SDL_Cursor *PortHwCursorNow(float fScale, const SDL_Color *pColors);
// Set by the presenter while the system shows it; the game's own drawing of the cursor stops
// meanwhile (cur_display_update, cur.cpp).
void PortHwCursorShown(bool bShown);
bool PortHwCursorShowing(void);
void PortHwCursorFree(void);

// With a HUD design over a game and no system cursor, the game's cursor is drawn above
// the HUD, the overlay's top layer, and not into the game's frame, which the HUD covers: true
// while it is (cur_display_update, cur.cpp).
bool PortCursorAbove(void);

// A GRP frame's pixels as palette indices, bWidth by bHeight, -1 where it is clear.
void PortGrpFrameDecode(const FRAME &f, std::vector<int> *pPixels);
