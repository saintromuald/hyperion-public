// port_labwall.h -- the AI lab's wall: a tile a game the farm plays, its map small with
// its resources and its units on it, moving as the samples come (port_labwall.cpp).
#ifndef PORT_LABWALL_H
#define PORT_LABWALL_H

#include <string>

namespace FrontKit {

// The wall at (x, y) in its panel, w by h in SC2's units: as many tiles as the farm has games.
// bHidden draws each game as how far it has gone, with no map (its workers send no samples).
void LabWall(float x, float y, float w, float h, bool bHidden);
// The farm's slot whose tile was clicked this frame, or -1.
int LabWallClicked(void);
// A map's picture as the wall keeps it, fitted in (x, y, w, h), pixels, in a layout's draw; a
// plain field when it has none.
void LabMapPicture(float x, float y, float w, float h, const std::string &path, float fAlpha);

}  // namespace FrontKit

#endif
