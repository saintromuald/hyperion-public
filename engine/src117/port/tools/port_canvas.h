// port_canvas.h -- a map pushed over the live channel: the canvas
// (port_canvas.cpp) made at a size and with the tiles given, in place of its plain ground.
#ifndef PORT_CANVAS_H
#define PORT_CANVAS_H

#include <string>
#include <vector>
#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

struct PortCanvasMap {
  std::string tileset;               // "badlands", "jungle", ... or "0" to "7"
  int nWidth = 64, nHeight = 64;     // in tiles: 64, 96, 128, 192 or 256 each
  std::vector<unsigned short> tiles; // MTXM's values a row at a time (group * 16 + variation);
                                     // empty for the canvas's plain ground
  struct Rect {
    int x, y, w, h;                  // in tiles
    unsigned short tile;
  };
  std::vector<Rect> rects;           // painted over the tiles or the ground, in turn
};

// Whether the map can be made: a tileset 1.17 has, a size it plays, a tile for each cell and
// each tile a group of the tileset's; *pWhy says what is wrong.
bool PortCanvasCheck(const PortCanvasMap &map, std::string *pWhy);
// The next canvas made (PortCanvasWrite) is this map.
void PortCanvasPushNext(const PortCanvasMap &map);
// The pushed map is forgotten: the game it was pushed for never started, and the next plain
// canvas is plain ground again (PortControlLaunchFailed).
void PortCanvasPushCancel(void);

// The canvas's tileset when none is named: Jungle, whose plain ground is grass (the user,
// 2026-10-27: "a more generic open area map with grasslands").
#define PORT_CANVAS_TILESET "jungle"

// Debug Tools' canvas (port_canvas.cpp): makes the map, of the tileset named
// (PORT_CANVAS_TILESET's when NULL or empty), in the data folder's mods\debug-tools\, and gives its
// path. PortLaunchCanvas makes it and sets it to start as a single-player game, from the
// menus; TRUE when it will. The canvas has no units; PortCanvasBattle(true) gives the next
// one made two armies and waves sent to fight (as HYPERION_CANVAS_BATTLE=1 gives every one).
bool PortCanvasWrite(const char *pszTileset, char *pszPath, size_t cchPath);
void PortCanvasBattle(bool bOn);

#endif
