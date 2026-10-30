// port_hd.h -- StarCraft: Remastered's HD art, read from the player's install
// (docs/modules/render.md, "HD art"). Each image of images.dat has its
// frames in HD2/anim/main_NNN.anim (2x) or anim/main_NNN.anim (4x); an image without a
// file of its own borrows another's, as SD/mainSD.anim names it. The layers are DDS
// files, DXT1 or DXT5, kept here as their blocks for the GPU to take as they are.
//
// Read on first use on a loader thread, so a frame never waits for the storage; until an
// image is in, it is drawn as 1.17 draws it. Nothing is needed: without an install
// PortHdImage always says no. Presentation only.
#ifndef PORT_HD_H
#define PORT_HD_H

#include <vector>

enum { PORT_HD_IMAGES = 999 };

// One frame: where it is in the layers' textures and where it is drawn on the image's
// canvas, in the textures' pixels.
struct PortHdFrame {
  unsigned short nX, nY;
  short nOffsetX, nOffsetY;
  unsigned short nWidth, nHeight;
};

// A layer: its blocks, BC1 (DXT1) or BC3 (DXT5), or its pixels as they are (a mod's sheet,
// port_modart.h), and its size in pixels.
enum { PORT_HD_RGBA8 = 8, PORT_HD_R8 = 9 };
struct PortHdLayer {
  int nBc;  // 1 or 3, PORT_HD_RGBA8 or PORT_HD_R8; 0 when the image has no such layer
  int nWidth, nHeight;
  std::vector<unsigned char> blocks;
};

// The lighting's layers (hd.hlsl, HdLit): the emissive glow, the normal (x in red, y in
// alpha), the specular colour, and ambient occlusion (green) with height (alpha).
enum PortHdLayerKind {
  PORT_HD_DIFFUSE,
  PORT_HD_TEAM,
  PORT_HD_BRIGHT,
  PORT_HD_EMISSIVE,
  PORT_HD_NORMAL,
  PORT_HD_SPECULAR,
  PORT_HD_AO_DEPTH,
  PORT_HD_LAYERS
};

struct PortHdArt {
  int nImage;              // the image whose file this is (itself, or the one it borrows)
  unsigned nSerial;        // 0 for Remastered's; a mod sheet's art its own number
  int nScale;              // texture pixels to a game pixel: 2 or 4
  int nCanvasW, nCanvasH;  // the image's canvas in texture pixels
  std::vector<PortHdFrame> frames;
  PortHdLayer layers[PORT_HD_LAYERS];
};

// A tileset's HD terrain: HD2/TileSet/<name>.dds.vr4 (TileSet/ at 4x), a 64x64 (128x128)
// DXT1 frame a megatile in the tileset's VX4 order, put side by side into one BC1 atlas,
// PORT_HD_TILES_ROW megatiles to a row. A frame that is not DXT1 at the tile's size is
// left black. Its creep edges too: <name>.dds.grp beside it, the HD frames of the edge GRP
// 1.17 draws over a megatile at creep's border, each in a cell a megatile's size,
// PORT_HD_CREEP_ROW to a row, its size kept.
enum { PORT_HD_TILESETS = 8, PORT_HD_TILES_ROW = 64, PORT_HD_CREEP_ROW = 8, PORT_HD_CREEP_MAX = 64 };

struct PortHdTerrain {
  int nEra;       // the tileset, as the map's ERA names it: 0 badlands to 7 twilight
  int nTiles;     // megatiles
  int nTileSize;  // a megatile's pixels across: 32 times the scale
  PortHdLayer atlas;
  int nCreep;     // creep edge frames, 0 when the file is not there
  unsigned int anCreepSize[PORT_HD_CREEP_MAX];  // each one's width, and its height above
  PortHdLayer creep;
  // Its water: <name>_mask.dds.grp, a mask for each megatile with water in it, white where
  // the water is, in a megatile's cell each, PORT_HD_TILES_ROW to a row; <name>.tmsk, which
  // megatile takes which mask; and the two animated normal maps Remastered's ripple reads,
  // effect/water_normal_1 and _2, a frame a cell, PORT_HD_CREEP_ROW to a row.
  int nWaterMasks;                    // 0 when the tileset has no water files
  PortHdLayer waterMask;
  std::vector<unsigned int> waterOf;  // each megatile's mask plus 1, 0 for one without water
  int anNormalFrames[2];
  int anNormalCell[2];                // a frame's texels across
  PortHdLayer normals[2];
  // Its star field, on the space platform only: parallax/star.spk and star.dds.grp (under
  // HD2/ at 2x), five layers in a field of 648 x 488 game pixels at the scale, each star a
  // rectangle of its layer's frame. The frames go into one BC3 atlas, one under another in
  // cells of nStarCellW x nStarCellH; the stars into starBins (hd.hlsl, HdStars): for each
  // layer the start of each nStarBin-texel square's list, the lists, then from nStarsAt the
  // stars, three words each (x and y in the field, x and y in the atlas, width and height).
  int nStarLayers;                    // 0 without a star field
  int nStarFieldW, nStarFieldH;
  int nStarCellW, nStarCellH;
  int nStarBin, nStarBinsX, nStarBinsY;
  unsigned int nStarsAt;
  PortHdLayer starAtlas;
  std::vector<unsigned int> starBins;
};

// The console's HD art (R4f): Remastered's game/consoles/<skin>/ (under HD2/ at 2x). A
// race's is three pieces, console_left, _center and _right, the centre drawn last since
// the sides reach under it; the neutral one a replay shows is one, console.DDS. Each sits
// on the screen's bottom, nX texels from the left of 1.17's 640-wide console. Its windows
// are where 1.17's are, the centre and right pieces over 1.17's at the scale, and the
// sides reach past it into a wider view.
enum { PORT_HD_CONSOLES = 4 };  // zerg, terran, protoss, as 1.17's race byte; neutral

struct PortHdConsolePiece {
  int nX;
  PortHdLayer art;
};

struct PortHdConsole {
  int nSkin;  // 0 zerg, 1 terran, 2 protoss, 3 neutral
  int nPieces;
  PortHdConsolePiece pieces[3];
};

// The console's HD contents (R4f): the command icons (unit/cmdicons/cmdicons.dds.grp, DXT1,
// in an atlas of 64-texel cells at 2x), each race's button frames
// (game/consoles/<race>/cmdbtns.dds.grp, 144-texel cells: up and pressed, the slots') and the
// wireframes (unit/wirefram/wirefram.dds.grp, DXT5, one 1024 x 128 frame a unit type: eight
// 128-texel cells, 0 to 3 the parts 1.17 colours one by one, 4 and 5 the shields' layers,
// 6 the whole unit in grey). Under HD2/ at 2x. Then each race's progress bar
// (game/consoles/<race>/pbrempt.dds and pbrfull.dds, DXT1): an atlas of the two, empty above
// full, nCell texel rows apart. Then the resource counter's icons
// (game/icons.dds.grp, 1.17's game\\icons.grp at the HD scale), drawn over the map (B8).
enum {
  PORT_HD_UI_ICONS = 0,
  PORT_HD_UI_BUTTONS = 1,
  PORT_HD_UI_WIRES = 4,
  PORT_HD_UI_BARS = 5,
  PORT_HD_UI_RESOURCES = 8,
  PORT_HD_UI_SETS = 9
};

struct PortHdUi {
  int nSet;
  int nCell, nRow, nFrames;           // an atlas's cells: their size, how many to a row
  std::vector<unsigned int> sizes;    // each frame's width, and its height above
  PortHdLayer atlas;
  std::vector<PortHdLayer> frames;    // the wireframes, one a unit type
};

// What the console shows that has HD art, read from its controls each frame
// (port_console.cpp, PortConsoleHdItems): a frame of a PortHdUi set (a wireframe's cell
// when nCell is 0 to 7) centred in an SD control's box on the screen, in game pixels, moved
// by nDX, nDY; multiplied by the brightest of the nTints palette colours abTint names, as
// 1.17 colours the frame through its remap, or drawn as it is with none. A progress bar's
// full frame (PORT_HD_UI_BARS + race, frame 1) is drawn nCell percent of its width.
struct PortHdUiItem {
  int nSet;
  int nFrame;
  int nCell;
  int nX, nY, nW, nH;
  int nDX, nDY;
  int nTints;
  unsigned char abTint[16];
};
// bConsole: Remastered's console is drawn, and all of them; else only a mod's own icons,
// wireframe and portrait, drawn over the classic console (PORT_HD_UI_MOD, PortConsoleModUi).
int PortConsoleHdItems(PortHdUiItem *pItems, int nMax, bool bConsole);
// Where the map shows through 1.17's drawing over it, because an HD item takes the place of
// what 1.17 drew there (the resource icons, B8): boxes in game pixels, what the last
// PortConsoleHdItems found.
int PortConsoleHdMapBoxes(int (*panBoxes)[4], int nMax);
// The numbers 1.17 writes over the selection panel's icons, which the HD UI pass
// keeps over the HD icons as 1.17 drew them, as the console's other text is: the box of the
// pixels the text changed, in game pixels, and which of them it did, a bit each, a row after
// another. What the last PortConsoleHdItems found.
enum { PORT_HD_TEXT_WORDS = 8 };
struct PortHdUiText {
  int nX, nY, nW, nH;
  unsigned int adwBits[PORT_HD_TEXT_WORDS];
};
int PortConsoleHdTexts(PortHdUiText *pTexts, int nMax);

// The portrait in HD: Remastered's video of the portrait 1.17 plays in the
// console, at the HD scale (HD2\portrait\ or portrait\), on the frame 1.17's has reached, as
// RGBA; nSerial changes with the picture. NULL while there is none. An item of
// PORT_HD_UI_PORTRAIT draws it over the portrait's control, the box covered and cut to it.
struct PortHdPortrait {
  int nWidth, nHeight;
  const unsigned char *pRgba;
  unsigned nSerial;
  bool bMod;  // a mod's own video, drawn with or without Remastered's art
};
const PortHdPortrait *PortHdPortraitFrame(void);
enum { PORT_HD_UI_PORTRAIT = 16 };
// An item of a mod's own icon or wireframe (port_modart.h, PortModUi): nFrame its group times
// 65536 and its frame, nCell the part it draws (0 all, 1 to 6 a wireframe's), each part with its
// own tint as the classic remap colours it.
enum { PORT_HD_UI_MOD = 17 };

// The scale asked for: 2 (HD2) unless HYPERION_HD_SCALE is 4.
int PortHdScale(void);
// Whether there is Remastered's art to draw with: its storage open. Asked again while it is
// not, as Settings can name an install later.
bool PortHdAvailable(void);
// Image nImage's art when it is in; NULL while it loads, and when there is none. The
// first call asks for it.
const PortHdArt *PortHdImage(int nImage);
// Tileset nEra's terrain, the same way.
const PortHdTerrain *PortHdTileset(int nEra);
// Console nSkin's art, the same way.
const PortHdConsole *PortHdConsoleArt(int nSkin);
// One of the console's contents (PORT_HD_UI_ICONS, PORT_HD_UI_BUTTONS + race, or
// PORT_HD_UI_WIRES), the same way.
const PortHdUi *PortHdUiSet(int nSet);
// Whether a layer's texel at x, y is solid, read from its blocks (alpha at least half).
bool PortHdLayerSolid(const PortHdLayer &layer, int x, int y);
// How many images are in, being read, and have none, for the log and the channel.
void PortHdCounts(int *pnReady, int *pnLoading, int *pnNone);

#endif
