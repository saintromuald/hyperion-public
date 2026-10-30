// port_selection.h -- the selection's look: the looks over 1.17's circles and health bars, a
// mod's among them (port_selection.cpp).
#ifndef PORT_SELECTION_H
#define PORT_SELECTION_H

// The selection's marks, as 1.17 draws them: the circle under a unit the player selected (images
// 0x231 to 0x23A), the circle under a teammate's (0x23B to 0x244), and the health bar over a
// selected unit (the image of palette type 0x0B).
enum { SELECTION_CIRCLE, SELECTION_TEAM_CIRCLE, SELECTION_HEALTH_BAR, SELECTION_MARKS };

// The selection's look (port_selection.cpp). A look can leave a mark undrawn; 1.17's marks are
// drawn wherever no look says otherwise. The marks stay in the game as 1.17 makes them, only
// their drawing is a look's: what is selected is the game's, carried by commands, recorded in
// replays and held in saves (gameplay.md), and the sync hash leaves the marks out. A module
// adds a look with PORT_SELECTION_LOOK at file scope, and the active mod's hud.json gives one
// ("selection", docs/mod-data.md).
struct PortSelectionLook {
  const char *pszName;
  // Whether a mark of a unit of the slot nOwner is drawn, bShown being the answer so far.
  bool (*Shown)(int nMark, int nOwner, bool bShown);
};

// Adds a look, asked after those added before it; false when the list is full. Looks add
// themselves as the program starts, in no fixed order between files.
bool PortSelectionAdd(const PortSelectionLook *pLook);
#define PORT_SELECTION_LOOK(look) static const bool s_bSelection_##look = PortSelectionAdd(&(look))

// CSprite::Draw's: whether an image of a sprite of the slot nOwner is drawn, true for an image
// that is no mark.
bool PortSelectionImageShown(int nImageId, int nPaletteType, int nOwner);

// The "selection" of the active mod's hud.json, as each console loads (port_console.cpp).
struct PortJson;
void PortSelectionHud(const PortJson &selection);

#endif
