// port_maptext.h -- a map's text as StarCraft: Remastered reads it: a character of
// several bytes is one character, UTF-8 first, and on a map whose strings are Korean a pair of
// bytes that is not UTF-8 is read as CP949, which Korean maps' programs write; anything else is
// a byte of its own in Windows-1252, as in 1.17. A character 1.17's fonts lack is drawn from a
// TrueType face in 1.17's glyph form, so it takes the text's colours and shadow as the font's
// own glyphs do (gds_font.cpp). The faces are StarCraft: Remastered's (font\Kostar.ttf, then
// font\bl.ttf) when its install is found, else the system's. Presentation only: what a string
// holds, and what the game does with it, are as they were.
#ifndef PORT_MAPTEXT_H
#define PORT_MAPTEXT_H

#include <stddef.h>
#include <string>

struct stbtt_fontinfo;

// The map's string section as it is read (maphdr_STR, maphdr_STRx; bWide for STRx, whose
// count and offsets are 4 bytes): whether any of its strings is Korean, in UTF-8 or in CP949.
void MapTextStrings(const void *pStrings, size_t cb, bool bWide);

// The character of several bytes at p, its bytes before pEnd (NULL for a NUL-ended string):
// its byte count, with its code point in *pcp; 0 when p is a byte of its own.
int MapTextChar(const char *p, const char *pEnd, unsigned *pcp);

// p moved back to the first byte of the character of several bytes it falls inside, in the
// string at pStart; p itself when it is not inside one.
const char *MapTextCharStart(const char *pStart, const char *p);

// The Windows-1252 byte, the game fonts' set, for a code point; -1 when it has none.
int MapTextByte1252(unsigned cp);

// A glyph for a code point in 1.17's form (a width, height, x and y offset, then the bits
// draw_char reads), made from a TrueType face to sit in a font of nMaxHeight pixels whose
// letters stand on row nBaseline; kept for the process's life, one for each height, baseline
// and code point. NULL when no face has the character.
const void *MapTextGlyph(int nMaxHeight, int nBaseline, unsigned cp);

// The width MapTextGlyph's glyphs are drawn at, in percent of their own, 100 unless a line is
// being narrowed to fit its place (dlg_draw.cpp's FitText); the game's own letters keep theirs.
void MapTextSqueeze(int nPercent);

// A map's string in UTF-8, for Hyperion's own screens (the map list): UTF-8 kept, a CP949 pair
// that makes a Hangul syllable read as one, any other byte as Windows-1252, and the colour
// codes (bytes under 32) left out. The map list reads many maps, so no one map's verdict holds.
std::string MapTextUtf8(const char *psz);

// The face, of those MapTextGlyph draws from, that has a code point, and the glyph's index in
// it; NULL when none has it. For Hyperion's own screens when their face lacks a character
// (port_ui.cpp).
const stbtt_fontinfo *MapTextFace(unsigned cp, int *pnGlyph);

// A glyph's bitmap box at a scale and a sub-pixel shift, as stb_truetype's own, but from its
// outline: some faces' composite glyphs record a box in their header that misses part of the
// glyph (Kostar's Hangul by 200 units of 1,000), and stb_truetype reads the header's.
void MapTextGlyphBox(const stbtt_fontinfo *pFont, int nGlyph, float fScaleX, float fScaleY, float fShiftX,
                     int *px0, int *py0, int *px1, int *py1);
// The glyph drawn into an nW by nH bitmap whose top left is the box's (x0, y0).
void MapTextGlyphDraw(const stbtt_fontinfo *pFont, int nGlyph, unsigned char *pOut, int nW, int nH, int nStride,
                      float fScaleX, float fScaleY, float fShiftX, int x0, int y0);

#endif
