// port_labgames.h -- the AI lab's games: every game the farm has played, from its
// project's games.jsonl, newest first; one picked, its players and how it went (port_labgames.cpp).
#ifndef PORT_LABGAMES_H
#define PORT_LABGAMES_H

#include <string>

namespace FrontKit {

// The view at (x, y) in its panel, w by h in SC2's units: the table, and the picked game beside it.
void LabGames(float x, float y, float w, float h);

}  // namespace FrontKit

#endif
