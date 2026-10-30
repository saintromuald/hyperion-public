// port_lights.h -- the 2D lights on the classic art. 1.17's art has no light
// data, so a table names the
// images that give light (explosions, fire, plasma, the Archon, muzzle flashes, engine
// glows) with a colour, a radius, an intensity and a flicker. Each frame CSprite::Draw
// tells of every image it draws (PortLightImage, below), and those in the table become
// lights where the image is. The GPU presenter adds them into a light buffer that lights
// the map (shaders/effects.hlsl, PSLights and PSPost; docs/renderer.md).
//
// The table is built in, and lights.json in the data folder replaces it when it is
// there; the live channel lists it, changes an entry and saves it (port_control.cpp).
// Presentation only: the lights read the game and change nothing in it.
#ifndef PORT_LIGHTS_H
#define PORT_LIGHTS_H

#include <string>

struct PortJson;

// One light of the frame, as the light pass reads it: in the frame's pixels (the map
// layer starts at its top), its reach, its strength with the flicker taken in, its colour.
struct PortLightGpu {
  float fX, fY, fRadius, fIntensity;
  float fR, fG, fB, fPad;
};

enum { PORT_LIGHTS_MAX = 512 };

// The lights of the last map pass, at most nMax; none outside a game.
int PortLightsFrame(PortLightGpu *pOut, int nMax);

// The table as JSON ({"lights": [...], "frame": <how many lit the last pass>}), an entry
// set from a channel message ({image, name?, color?: "#rrggbb", radius?, intensity?,
// flicker?, remove?}), and the table saved as lights.json in the data folder.
std::string PortLightsJson(void);
bool PortLightSet(const PortJson &j, std::string *pError);
bool PortLightsSave(std::string *pPath);

// The 2D lights (port_lights.cpp): CSprite::DrawAll begins and ends each
// map pass's lights, and CSprite::Draw tells of each image it draws, by its images.dat
// place and its map position. Reading only.
void PortLightsBegin(void);
void PortLightImage(unsigned int nImage, int nMapX, int nMapY);
void PortLightsEnd(void);

#endif
