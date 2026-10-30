// light.cpp -- palette colour cycling and the tileset's light tables, oracle 0x4666C0
// through 0x466990. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/light.h"
#include "../h/CImage.h"       // light_t, the element of light_g
#include "../gen/globals117.h"
#include "../h/game.h"         // CPalette
#include "../h/gds_palette.h"  // palentry_t
#include "../h/gamemap.h"      // colorcycling_free / light_free / sub_466810 / sub_4668F0
#include "../h/glues.h"        // light_cycle_init / light_init / GameLoadImage / mem_free
#include "../h/gds_local.h"    // psnprintf
#include "../h/snd.h"          // load_game_data
#include "../h/maphdr.h"       // is_color_cycled
#include "../h/Storm.h"        // SMemAlloc / SMemFree / SBmpDecodeImage

// 0x66FD20 holds a pointer, not the array: the body loads it with a dword move and then
// indexes it a byte at a time.
#ifdef OC_DLL_MODE
#define sgpbIsCyclingColor (*reinterpret_cast<BYTE **>(OC_VA(0x66FD20u)))
#else
extern BYTE *sgpbIsCyclingColor;
#endif

// The nearest palette slot to one colour, by squared RGB distance. Entry 0 is skipped and
// any slot sgpbIsCyclingColor marks is passed over. The colour is an aggregate, so
// __fastcall pushes it: ECX plus one dword plus `ret 4h`.
int __fastcall CPalette::find_closest_palette_entry(void *pPalette, palentry_t entry)
{
  palentry_t *&pEntry = reinterpret_cast<palentry_t *&>(pPalette);
  DWORD dwBest = 0xFFFFFFFFu;
  int nBest = 0;
  ++pEntry;

  for (int i = 1; i < 256; ++i, ++pEntry) {
    if (sgpbIsCyclingColor[i] != 0)
      continue;

    int dr = entry.bRed - pEntry->bRed;
    int dg = entry.bGreen - pEntry->bGreen;
    int db = entry.bBlue - pEntry->bBlue;
    DWORD dwDist = static_cast<DWORD>(db * db + dg * dg + dr * dr);
    if (dwDist < dwBest) {
      nBest = i;
      dwBest = dwDist;
    }
  }
  return nBest;
}

// Releases the colour-cycling flag table. The store of NULL is outside the null test, so
// it runs whether or not the table was allocated.
void colorcycling_free()
{
  if (sgpbIsCyclingColor != NULL)
    SMemFree(sgpbIsCyclingColor, "light.cpp", 0x78, 0);
  sgpbIsCyclingColor = NULL;
}

// Frees each light table's decoded rows. Clearing pData runs on both arms of the null test.
void light_free()
{
  int i = 7;
  light_t *pLight = light_g;
  while (i--) {
    if (pLight->pData != NULL)
      SMemFree(pLight->pData, "light.cpp", 0xA6, 0);
    pLight->pData = NULL;
    ++pLight;
  }
}

// Builds the 256-byte table of which palette slots the tileset cycles. The
// global is re-read every iteration because is_color_cycled could move it.
void light_cycle_init()
{
  sgpbIsCyclingColor = static_cast<BYTE *>(SMemAlloc(0x100, "light.cpp", 0x6F, 0));

  int i = 256;
  while (i--)
    sgpbIsCyclingColor[i] = static_cast<BYTE>(is_color_cycled(i));
}

// Loads the seven light .pcx tables for one tileset. Each is decoded twice:
// once to measure, once into a buffer of (height + 1) * 256 whose first 256
// bytes are left for the ramp the blitters index from light_g[0].pData.
void sub_466810(BYTE *pPalette, const char *pszTileset)
{
  char szPath[0x104];
  DWORD dwSize;
  int nHeight;
  int nWidth;

  int i = 7;
  light_t *pLight = light_g;
  while (i--) {
    psnprintf(szPath, 0x104, "Tileset\\%s\\%s.pcx", pszTileset, pLight->szName);
    void *pData = load_game_data(szPath, NULL, &dwSize, "light.cpp", 0xB5);
    SBmpDecodeImage(2, pData, dwSize, 0, 0, 0,
                    (DWORD_PTR)&nWidth, (DWORD_PTR)&nHeight, 0);

    DWORD cbRows = static_cast<DWORD>(nHeight + 1) << 8;
    pLight->pData = static_cast<BYTE *>(SMemAlloc(cbRows, "light.cpp", 0xCD, 0));
    SBmpDecodeImage(2, pData, dwSize, 0, pLight->pData + 0x100, cbRows - 0x100,
                    0, 0, 0);
    mem_free(pData, "light.cpp", 0xD8);
    ++pLight;
  }
}

// Loads the tileset's dark ramp and resets both remap tables to the identity.
void sub_4668F0(BYTE *pPalette, const char *pszTileset)
{
  char szPath[0x104];

  for (unsigned int i = 0; i < 256; ++i)
    _gbRemapColorTbl[i] = static_cast<BYTE>(i);

  psnprintf(szPath, 0x104, "Tileset\\%s\\dark.pcx", pszTileset);
  GameLoadImage((intptr_t)szPath, 0, (intptr_t)shiftDark_g, 0x2000, 0, 0, 0);

  for (unsigned int i = 0; i < 256; ++i)
    shiftDark_g[31][i] = static_cast<BYTE>(i);
}

// Resets the remap table to the identity.
void light_init()
{
  for (unsigned int i = 0; i < 256; ++i)
    _gbRemapColorTbl[i] = static_cast<BYTE>(i);
}
