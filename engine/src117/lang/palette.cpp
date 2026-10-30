// palette.cpp -- the font palette loaders. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/palette.h"
#include <string.h>
#include "../h/glues.h"         // GameLoadImage
#include "../gen/globals117.h"

// 0x6D3050 fontpalette_g -- 24 font colour ramps of eight entries each, spelled for both
// builds.
#ifdef OC_DLL_MODE
#define fontpalette_g (reinterpret_cast<BYTE (*)[8]>(OC_VA(0x6D3050u)))
#else
extern BYTE fontpalette_g[24][8];
#endif

// Loads a font palette from a .pcx.
void load_font_palette(const char *pszFile) {
  memset(fontpalette_g, 0, sizeof(BYTE[24][8]));
  GameLoadImage(reinterpret_cast<intptr_t>(pszFile), 0,
                reinterpret_cast<intptr_t>(fontpalette_g), 0xC0, 0, 0, 0);
}

// Loads the in-game font palette.
void init_game_font_palette() {
  memset(fontpalette_g, 0, sizeof(BYTE[24][8]));
  GameLoadImage(reinterpret_cast<intptr_t>("game\\tFontGam.pcx"), 0,
                reinterpret_cast<intptr_t>(fontpalette_g), 0xC0, 0, 0, 0);
}

// Copies a whole palette into the font palette.
void set_font_palette(const BYTE *pPalette) {
  memcpy(fontpalette_g, pPalette, sizeof(BYTE[24][8]));
}
