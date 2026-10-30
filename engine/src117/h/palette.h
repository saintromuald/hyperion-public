// palette.h -- declarations for src117/lang/palette.cpp, the font palette
// loaders, 0x47CA80 to 0x47CB12.
// The free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_PALETTE_H
#define OC117_PALETTE_H

#include "platform.h"

// 0x47CA80. ECX is the .pcx path.
void load_font_palette(const char *pszFile);

// 0x47CAC0.
void init_game_font_palette();

// 0x47CB00. ECX is the 0xC0-byte source copied into the font palette.
void set_font_palette(const BYTE *pPalette);

#endif  // OC117_PALETTE_H
