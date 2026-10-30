// port_hotkeys.h -- the command card's keys and 1.17's accelerators as the hotkey profile sets
// them (port_hotkeys.cpp).
#ifndef PORT_HOTKEYS_H
#define PORT_HOTKEYS_H

// The command card's keys as the profile sets them (port_hotkeys.cpp, the hotkeys milestone
// K1): PortCardKey is the key of the button whose stat_txt string is wStrId (pszText) in
// place nSlot (1 to 9) of the card, lower case; PortCardTooltip the text its tooltip shows,
// the key it has now marked. The layout is Classic (1.17's keys) or Grid (Q W E / A S D /
// Z X C by place); PortHotkeysLoad reads the profile's as a game starts.
enum { PORT_CARD_CLASSIC, PORT_CARD_GRID };
void PortHotkeysLoad(void);
int PortCardLayout(void);
void PortCardLayoutSet(int nLayout);
char PortCardKey(unsigned wStrId, int nSlot, const char *pszText);
const char *PortCardTooltip(unsigned wStrId, int nSlot, const char *pszText);
// K2: an accelerator table's entries (ACCEL's, 6 bytes each) with the profile's keys for its
// commands in place of 1.17's; the count written. PortAccelProfiled: the profile binds any.
int PortAccelRemap(const void *pIn, int nIn, void *pOut, int nCap);
// K3: the control group a typed character selects, as the profile binds it; -1 for none.
int PortKeyGroup(unsigned nChar);
bool PortAccelProfiled(void);

#endif
