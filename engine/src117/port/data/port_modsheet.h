// port_modsheet.h -- a mod's art sheet as classic's frames (port_modsheet.cpp).
// An image record of a mod's may name a picture, a sheet of its frames, in place of a GRP
// (docs/mod-data.md, "Sheets"); Hyperion makes the GRP from it as the mod loads, which the
// game's sizes and the classic picture use.
#ifndef PORT_MODSHEET_H
#define PORT_MODSHEET_H

#include <string>
#include <vector>

// A sheet: its cells, nFrameW by nFrameH game pixels, read along each row and down; the
// origin, where the sprite's position falls in a cell, in game pixels; nFrames cells, or 0 for
// every one; and the size of each level's picture, at nScale of its pixels to a game pixel's
// side, the smallest scale first.
struct ModSheetLevel {
  int nScale = 1;
  int w = 0, h = 0;
};
struct ModSheet {
  int nFrameW = 0, nFrameH = 0;
  double fOriginX = 0, fOriginY = 0;
  int nFrames = 0;
  std::vector<ModSheetLevel> levels;
};

// Whether the sheet's levels hold its cells alike: each a whole number of cells across and
// down, the same number at every level, and nFrames of them at least. False with the reason in
// *pError; *pnCells the cells there are.
bool ModSheetCheck(const ModSheet &sheet, int *pnCells, std::string *pError);

// An RGBA picture w by h made nScale times smaller: each pixel the average of its nScale by
// nScale, their colours weighted by their alpha.
std::vector<unsigned char> ModSheetScaledDown(const std::vector<unsigned char> &rgba, int w, int h, int nScale);

// A sheet's frames as classic encodes them: the canvas, and each frame's place and size on it,
// its rows' codes (0x80 | n clear, 0x40 | n of one index, n indexes) and where each row starts.
struct ModSheetFrame {
  int x = 0, y = 0, w = 0, h = 0;
  std::vector<unsigned> rows;
  std::vector<unsigned char> codes;
};
struct ModSheetFrames {
  int w = 0, h = 0;
  int dx = 0, dy = 0;  // where a cell's corner falls on the canvas
  std::vector<ModSheetFrame> frames;
};

// The sheet's frames, from its picture at scale 1 (rgba, and the team layer, team, the same
// size, or empty): in classic's colours, the nearest of those every tileset has (16 to 199), and
// the team layer's pixels the nearest step of player 1's colour (8 to 15), which the game gives
// each player's colour; every frame on one canvas centred on the origin, each cropped to its
// pixels. A pixel is drawn where its alpha is half or more. bShadow: an image of classic's shadow
// draw (its draw function 10), whose frames are the sheet's pixels with any alpha, as one
// index. False with *pError when the canvas is past 65,535 a side.
bool ModSheetEncode(const ModSheet &sheet, const std::vector<unsigned char> &rgba, const std::vector<unsigned char> &team,
                    bool bShadow, ModSheetFrames *pFrames, std::string *pError);

// The brightest channel of player 1's colour, in which a sheet's team pixels are taken to be
// drawn; 255 when classic's colours do not read.
int ModSheetTeamBright(void);

// The frames as a classic GRP file; false when one is past what a file holds, 255 pixels a
// side or 64 KiB of codes.
bool ModSheetFile(const ModSheetFrames &frames, std::vector<unsigned char> *pFile);

// The frames as a group the engine draws, in a block of SMemAlloc's: at 64 bits PortGroupNative's
// layout, each frame's size in WORDs and its rows' offsets in DWORDs (pLines), so a frame may be
// any size; at classic's widths the file (ModSheetFile), NULL if it does not hold them.
struct GROUP;
GROUP *ModSheetGroupNative(const ModSheetFrames &frames);

#endif
