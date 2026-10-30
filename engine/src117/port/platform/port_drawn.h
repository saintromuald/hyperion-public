// port_drawn.h -- a small window Hyperion draws itself (port_drawn.cpp), for what it shows
// before the game's window or without it on Linux and a Mac, which have no window of the
// system's that Hyperion can count on: the first start's (port_install_window.cpp) and the
// error window (port_errorwin.cpp).
//
// An SDL window and renderer of its own. The text is Hyperion's typefaces (Saira, and IBM Plex
// Mono for a block of text to copy), set with stb_truetype, each string drawn once into a
// texture. The layout is in units of a window fWidth wide, scaled by the screen's.
#ifndef PORT_DRAWN_H
#define PORT_DRAWN_H

#include <string>
#include <SDL3/SDL.h>

enum { PORT_DRAWN_TEXT, PORT_DRAWN_BOLD, PORT_DRAWN_MONO, PORT_DRAWN_FACES };

struct PortDrawnTheme {
  SDL_Color back, text, dim, field, line, accent, button, hover, disabled, found, missing;
};

struct PortDrawnFonts;  // port_drawn.cpp's: the faces, and the strings drawn with them

struct PortDrawn {
  SDL_Window *pWindow = nullptr;
  SDL_Renderer *pRenderer = nullptr;
  PortDrawnFonts *pFonts = nullptr;
  const PortDrawnTheme *pTheme = nullptr;
  float fWidth = 640.0f;
  float fScale = 1.0f;            // pixels to a unit
  float fMouseX = -1.0f, fMouseY = -1.0f;  // in units
};

// The window opened hidden, fWidth units wide, in the system's light or dark theme; false when
// it cannot be, *ppszFailed naming what failed (SDL_GetError has why).
bool PortDrawnOpen(PortDrawn &d, const char *pszTitle, float fWidth, const char **ppszFailed);
// Shown at fHeight units high, centred and raised.
void PortDrawnShow(PortDrawn &d, float fHeight);
void PortDrawnClose(PortDrawn &d);

// A string's width in units at a size of fSize units.
float PortDrawnTextWidth(const PortDrawn &d, int nFace, float fSize, const std::string &s);
// A line of text at (x, y), its top; its width in units. fClipWidth, when not 0, shows the end
// of a longer line in that width, as a field shows the end of what was typed.
float PortDrawnText(PortDrawn &d, int nFace, float fSize, const std::string &s, float x, float y, SDL_Color c,
                    float fClipWidth = 0.0f);
// Text wrapped at the spaces to fWidth units; its height in units.
float PortDrawnWrapped(PortDrawn &d, int nFace, float fSize, const std::string &s, float x, float y, float fWidth,
                       SDL_Color c);

void PortDrawnFill(PortDrawn &d, const SDL_FRect &r, SDL_Color c);
void PortDrawnOutline(PortDrawn &d, const SDL_FRect &r, SDL_Color c, float fThick);
void PortDrawnDisc(PortDrawn &d, float cx, float cy, float fRadius, SDL_Color c);
// Drawing kept inside r, or anywhere again with NULL.
void PortDrawnClip(PortDrawn &d, const SDL_FRect *r);
// A button, lit while the mouse is over it; the default one has the accent's outline.
void PortDrawnButton(PortDrawn &d, const SDL_FRect &r, const char *pszLabel, bool bEnabled, bool bDefault);
bool PortDrawnInside(const SDL_FRect &r, float x, float y);

// An event in the window's coordinates turned to the renderer's, the mouse's place kept in units.
void PortDrawnEvent(PortDrawn &d, SDL_Event &e);

#endif
