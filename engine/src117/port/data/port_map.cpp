// port_map.cpp -- what maps saved by StarCraft: Remastered add to 1.17's: the STRx string
// table and the CRGB player colours (further down).
//
// 1.17 keeps a map's STR section as it is on disk (gpMapStrs, maphdr_STR) and
// reads it through str_get_string: a u16 count, then u16 offsets from the
// section's start. Maps saved by Remastered and ScmDraft 2 may carry STRx
// instead: the same with a u32 count and u32 offsets, so the strings can pass
// 64 KB. maphdr_STRx keeps it the same way, and this remembers which
// of the two the table is, for get_map_string and the save game. Loading and
// text only; the simulation reads neither.
#include <string.h>
#include "port_map.h"
#include "../render/port_teamcolor.h"  // PortTeamColorSet
#include "oc_data.h"       // OC_VA, where 1.17's data is
#include "Storm.h"        // <windows.h>
#include "../../h/playertables.h"  // gdwTeamColorTbl_g, gbMinimapColor_g
#include "../../gen/globals117.h"
#include "../platform/port_globals117.h"

static int g_bWide;

void PortMapStringsWide(int bWide)
{
  g_bWide = bWide;
}

int PortMapStringsAreWide(void)
{
  return g_bWide;
}

// str_get_string's rules on the wider table: ids count from 1, 0 is NULL and an
// id past the count is the empty string. An offset past the table, which STR
// could not reach, is the empty string too, as in Remastered.
const char *PortMapStringWide(const void *pTable, ULONG dwSize, unsigned short wId)
{
  const BYTE *pBytes = static_cast<const BYTE *>(pTable);
  if (wId-- == 0)
    return NULL;
  if (dwSize < 4)
    return "";
  DWORD dwCount = *reinterpret_cast<const DWORD *>(pBytes);
  if (wId >= dwCount || 4 + 4 * (DWORD)wId + 4 > dwSize)
    return "";
  DWORD dwOffset = reinterpret_cast<const DWORD *>(pBytes + 4)[wId];
  if (dwOffset >= dwSize || memchr(pBytes + dwOffset, 0, dwSize - dwOffset) == NULL)
    return "";
  return reinterpret_cast<const char *>(pBytes + dwOffset);
}

// The same rules on a STR table. 1.17's str_get_string reads the count and the
// offset wherever they fall, and a protected map can carry a STR section too
// short to hold them: s3's map has a second STR of one byte, which replaces the
// first. 1.17 then reads what lies past the block in Storm's heap, and Hyperion's
// heap faulted when the bytes there made an offset into unmapped memory.
const char *PortMapStringNarrow(const void *pTable, ULONG dwSize, unsigned short wId)
{
  const BYTE *pBytes = static_cast<const BYTE *>(pTable);
  if (wId-- == 0)
    return NULL;
  if (dwSize < 2)
    return "";
  WORD wCount = *reinterpret_cast<const WORD *>(pBytes);
  if (wId >= wCount || 2 + 2 * (DWORD)wId + 2 > dwSize)
    return "";
  WORD wOffset = reinterpret_cast<const WORD *>(pBytes + 2)[wId];
  if (wOffset >= dwSize || memchr(pBytes + wOffset, 0, dwSize - wOffset) == NULL)
    return "";
  return reinterpret_cast<const char *>(pBytes + wOffset);
}

// ---------------------------------------------------------------------------
// The players' colours a map sets in CRGB, read as Remastered's loader reads it:
// eight RGB triples, then a byte a player. 0 (a predefined colour at random) and 1 (the
// player's choice) keep the colour 1.17 gives, 2 takes the triple, 3 and up name one of
// Remastered's 22 predefined colours in the triple's third byte; the ten past 1.17's twelve
// (tunit.pcx) have no ramp and keep 1.17's colour. An RGB colour's 8-bit ramps
// (gdwTeamColorTbl_g, gbMinimapColor_g) are those of 1.17's nearest colour rescaled to it
// step by step, among the palette entries 1.17's ramps use, which are the same on every
// tileset and never cycle; R4 draws the colour itself. Presentation only.
#define sgGamePalette_g (reinterpret_cast<const BYTE (*)[4]>(OC_VA(0x666568u)))

enum { CRGB_SIZE = 32, COLORS = 12, RAMP = 8, PLAYERS = 8 };

static BYTE g_abRamps[COLORS][RAMP];
static BYTE g_abMinimap[COLORS];
static BYTE g_abCrgb[CRGB_SIZE];
static int g_bCrgb;

void PortMapColorsBegin(void)
{
  memcpy(g_abRamps, gdwTeamColorTbl_g.a, sizeof g_abRamps);
  memcpy(g_abMinimap, gbMinimapColor_g.a, sizeof g_abMinimap);
  g_bCrgb = FALSE;
}

void PortMapColorsSet(const unsigned char *pCrgb)
{
  memcpy(g_abCrgb, pCrgb, CRGB_SIZE);
  g_bCrgb = TRUE;
}

static int ColorLuma(const BYTE *pRgb)
{
  return 299 * pRgb[0] + 587 * pRgb[1] + 114 * pRgb[2];
}

static int ColorDistance(const BYTE *pRgb, int r, int g, int b)
{
  int dr = pRgb[0] - r, dg = pRgb[1] - g, db = pRgb[2] - b;
  return 2 * dr * dr + 4 * dg * dg + 3 * db * db;
}

static BYTE ColorNearest(int r, int g, int b)
{
  BYTE bBest = g_abMinimap[0];
  int nBest = ColorDistance(sgGamePalette_g[bBest], r, g, b);
  for (int c = 0; c < COLORS; c++) {
    for (int k = 0; k < RAMP; k++) {
      int n = ColorDistance(sgGamePalette_g[g_abRamps[c][k]], r, g, b);
      if (n < nBest) {
        nBest = n;
        bBest = g_abRamps[c][k];
      }
    }
  }
  return bBest;
}

static void ColorsCustom(int nPlayer, const BYTE *pRgb)
{
  int nShape = 0;
  for (int c = 1; c < COLORS; c++)
    if (ColorDistance(sgGamePalette_g[g_abRamps[c][0]], pRgb[0], pRgb[1], pRgb[2]) <
        ColorDistance(sgGamePalette_g[g_abRamps[nShape][0]], pRgb[0], pRgb[1], pRgb[2]))
      nShape = c;
  int nTop = ColorLuma(sgGamePalette_g[g_abRamps[nShape][0]]);
  if (nTop == 0)
    nTop = 1;
  for (int k = 0; k < RAMP; k++) {
    int nStep = ColorLuma(sgGamePalette_g[g_abRamps[nShape][k]]);
    int r = pRgb[0] * nStep / nTop, g = pRgb[1] * nStep / nTop, b = pRgb[2] * nStep / nTop;
    gdwTeamColorTbl_g[nPlayer][k] = ColorNearest(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
  }
  gbMinimapColor_g[nPlayer] = ColorNearest(pRgb[0], pRgb[1], pRgb[2]);
}

void PortMapColorsApply(void)
{
  if (!g_bCrgb)
    return;
  for (int p = 0; p < PLAYERS; p++) {
    const BYTE *pRgb = &g_abCrgb[3 * p];
    BYTE bMode = g_abCrgb[3 * PLAYERS + p];
    if (bMode == 2) {
      ColorsCustom(p, pRgb);
      PortTeamColorSet(p, pRgb[0], pRgb[1], pRgb[2], FALSE);  // the colour itself, for full colour
    } else if (bMode >= 3 && pRgb[2] < COLORS) {
      memcpy(gdwTeamColorTbl_g[p], g_abRamps[pRgb[2]], RAMP);
      gbMinimapColor_g[p] = g_abMinimap[pRgb[2]];
    }
  }
}
