// port_modart.h -- a mod's art sheets at their own resolution, for the HD frame
// (port_modart.cpp). The classic frames a sheet makes (port_modsheet.h) are what the game's
// sizes and the software picture use; the GPU draws the same frames from the sheet's pixels
// in full colour, at the HD frame's scale, whether or not Remastered's art is drawn.
#ifndef PORT_MODART_H
#define PORT_MODART_H

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "port_hd.h"

// What an image's sheet gives the HD frame: its cells, nFrameW by nFrameH game pixels, nFrames
// of them read along each row and down; each level's files (parts stacked top to bottom) and
// its team layer's, at nScale of their pixels to a game pixel; and the classic canvas the
// frames were made on, nCanvasW by nCanvasH game pixels with a cell's corner at nCellX, nCellY
// (ModSheetFrames), which the HD canvas matches at its scale.
struct PortModArtSheet {
  std::string folder;
  int nFrameW = 0, nFrameH = 0, nFrames = 0;
  struct Level {
    int nScale = 1;
    std::vector<std::string> parts, team;
  };
  std::vector<Level> levels;
  int nCanvasW = 0, nCanvasH = 0, nCellX = 0, nCellY = 0;
  bool bShadow = false;  // drawn as a shadow: no team layer
  int nTeamBright = 255;  // the brightest of the player colour a sheet's team pixels were drawn in
};

// An image's sheet, as a mod's data loads; an image with none (a GRP, or its base's frames) is
// set with NULL.
void PortModArtSet(int nImage, const PortModArtSheet *pSheet);
// Whether a game is one with the mod's data: the HD frame draws its sheets in it. A new game
// also frees the art that sets replaced.
void PortModArtGame(bool bOn);
// Whether the game drawn has any sheet, or icon or wireframe of the mod's own: the HD frame
// is wanted for it.
bool PortModArtWanted(void);
// Whether image nImage is a sheet's, whose art is PortModArtImage's alone.
bool PortModArtHas(int nImage);
// Its art at the HD scale: NULL while it is made and when it cannot be. The first call asks
// for it, made on a thread of its own.
const PortHdArt *PortModArtImage(int nImage);
// Changes with each game: the GPU lets the art of the one before go.
unsigned PortModArtGeneration(void);

// A mod's own command icons and wireframes at the HD scale, for the console. Each picture is
// RGBA: red the pixel's brightness, which the console's colour multiplies, green its part times
// 32 (0 an icon's; 1 to 4 a wireframe's parts, 5 and 6 its outline in the shields' two
// colours), and its alpha. nKey is the frame's group (ModArtKind) times 65536 and its frame.
struct PortModUiPicture {
  unsigned nKey = 0;
  int nW = 0, nH = 0;
  std::vector<unsigned char> rgba;
};
// The pictures packed into one, and where each frame is in it.
struct PortModUi {
  struct Frame {
    int nX, nY, nW, nH;
  };
  PortHdLayer atlas;
  std::map<unsigned, Frame> frames;
  unsigned nSerial = 0;
};
// The game's pictures, as each game with the mod begins (port_moddata.cpp, ArtBuild); none
// for a game without.
void PortModUiSet(const std::vector<PortModUiPicture> &pictures);
// The game's, for the GPU; empty when it has none.
std::shared_ptr<const PortModUi> PortModUiArt(void);
// Whether frame nFrame of group nKind has a picture in the game drawn.
bool PortModUiHas(int nKind, unsigned nFrame);

#endif
