// port_world.h -- the world behind the front end: a planet seen from orbit (port_world.cpp,
// docs/front-end.md, F1). Presentation only.
#ifndef PORT_WORLD_H
#define PORT_WORLD_H

// The planet shader's parameters, as shaders/ui.hlsl's PlanetParams has them: float4s only,
// so the layout is the same on both sides.
struct PlanetParams {
  float res[4];       // width, height, time, fade
  float camPos[4];    // xyz, tan(fov / 2)
  float camFwd[4];    // xyz, the clouds' drift
  float camRight[4];  // xyz, how much of the sun shows
  float camUp[4];     // xyz, the dimming behind the screen's panels
  float sun[4];       // xyz, how much of the second sun shows
  float sun2[4];      // xyz, how much the scorched ground shines
  float star2[4];     // the second sun's light, the night lights' brightness
  float spin[3][4];   // the planet's turn, as three columns
  float sunScreen[4]; // xy, continent size
  float seedV[4];
  float sea[4];       // sea level, moisture, ice, dunes
  float deep[4];
  float shallow[4];
  float land[4][4];
  float alt[2][4];
  float iceCol[4];    // rgb, craters
  float relief[4];    // relief, city, scar, creep
  float cityAt[4];
  float cityCol[4];
  float cityDay[4];
  float scarCol[4];
  float creepCol[4];
  float creepGlow[4];
  float lavaCol[4];   // rgb, lava
  float cloud[4];     // cover, bands, the air's thickness, the star's strength
  float cloudCol[4];
  float haze[4];
  float atmo[4];
  float star[4];
  float nebA[4];
  float nebB[4];
  float moon[3][4];
  float moonCol[3][4];
  float moonGas[4];
};

// The names the world's plate shows.
struct WorldAbout {
  const char *pszName;
  const char *pszArea;
  const char *pszNote;
  const char *pszState;  // "Infested", "Fallen", or NULL
};

// The world shown, by its slug ("korhal", or a state, "tarsonis:fallen"); an unknown slug
// shows Korhal. A change fades through black unless instant.
void WorldSetPlanet(const char *pszSlug, bool bInstant);
const char *WorldPlanet(void);
// The world chosen last (it may still be fading in).
void WorldAboutNow(WorldAbout *pAbout);
// The camera's view for a screen ("home", "campaign", "mods", "settings"); the camera
// travels to it unless instant.
void WorldSetView(const char *pszView, bool bInstant);
// Advances the world's clock and eases the camera and the fade.
void WorldStep(double dSeconds);
// The front end's first appearance (front.css, .orbit-host): the planet and what is in
// orbit come out of black over 1.1 s on the design's ease. WorldIntroSkip ends it.
void WorldIntroBegin(void);
void WorldIntroSkip(void);
void WorldParams(int nWidth, int nHeight, PlanetParams *pParams);

#endif
