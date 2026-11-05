// port_production.h -- the research opportunities and global production overlays
// (port_production.cpp).
#ifndef PORT_PRODUCTION_H
#define PORT_PRODUCTION_H

struct PortHdUiItem;

// One of Hyperion's own hotkeys (PORT_HOTKEY_RESEARCH or PORT_HOTKEY_PRODUCTION), pressed
// without a repeat. True when the key was taken. Research is Newbie mode's panel; production is
// available in every game. The panels themselves are presentation only; a research click sends
// the ordinary Research or Upgrade net command, and a source click the ordinary Select command.
bool PortProductionHotkey(int nHotkey);

// Additional HD command icons for the visible rows, in game-pixel rectangles. Appended to the
// console's items by PortConsoleHdItems only while Remastered's console is drawn. The overlay
// draws a small text badge instead when classic art is on, an icon is not ready, or the console's
// fixed item capacity leaves no room.
int PortProductionHdItems(PortHdUiItem *pItems, int nMax);

#endif
