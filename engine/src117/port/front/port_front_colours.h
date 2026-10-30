// port_front_colours.h -- the colours a set-up's row may take (port_front_colours.cpp; the design
// center's 11-front-end, lobby.js's colour model and teams.js's picker): the game's 65 and any
// colour, how the game draws each, the rule for two that read as one, and the Any colour field.
#pragma once
#include <vector>

namespace FrontKit {

// The game's 65 (1.17's twelve in tunit.pcx's order, then Remastered's and Hyperion's, as the
// game draws them, PlayersColourRamp), and COLOUR_ANY, a row's colour of its own in full RGB.
enum { COLOUR_GAME = 65, COLOUR_ANY = COLOUR_GAME };

// A colour's name, its swatch, and the picker's order of the 65: round the hue wheel, the greys
// last.
const char *ColourName(int nColour);
unsigned ColourRgb(int nColour);
const std::vector<int> &ColourOrder(void);

// How the game draws a colour, one of the 65 or (COLOUR_ANY) rgb: the units' eight shades and
// the minimap's dot. Any colour is fitted as port_teamcolor.cpp fits it: the shades follow the
// ramp of 1.17's sixteen whose first is nearest, and the dot is the nearest of their entries.
struct ColourLook {
  unsigned anShade[8];
  unsigned nMinimap;
  float afShade[8][3], afMinimap[3];  // OKLab
};
const ColourLook &ColourLookOf(int nColour, unsigned rgb);

// How far apart two colours look: the nearer of the minimap's dots and the units' shades, each
// in OKLab (lobby.js dist). Under kColourSame they read as one, and a second row cannot take it
// (the game's colours swap instead); under kColourClose they are close, allowed and marked.
// ColourCloseOn says which of the two they are close on.
const float kColourSame = 0.04f, kColourClose = 0.08f;
float ColourGap(int nA, unsigned rgbA, int nB, unsigned rgbB);
const char *ColourCloseOn(int nA, unsigned rgbA, int nB, unsigned rgbB);

// The Any colour field (teams.js any): a saturation and brightness field with the hue strip
// beside it, dragged or moved with the arrow keys (Shift for steps of 10), and Hex, R, G and B to
// type in. The field takes the column it is laid out in; true when *pRgb changed. While an entry
// has the keyboard ColourAnyTyping says so, and the set-up leaves the keys to it.
bool ColourAnyField(unsigned *pRgb, bool bLive);
void ColourAnyValues(unsigned *pRgb, bool bLive);
bool ColourAnyTyping(void);
bool ColourAnyEntryFocused(void);  // the same, without giving the keys back
void ColourAnyReset(void);

}  // namespace FrontKit
