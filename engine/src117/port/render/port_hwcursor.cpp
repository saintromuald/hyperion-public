// port_hwcursor.cpp -- Settings' "Hardware cursor" (docs/modules/render.md): the game's cursor
// frame made into a system cursor, so the cursor moves with the mouse between the game's frames
// instead of with each frame drawn. The frame is 1.17's own cursor art, the one cur.cpp would
// draw, scaled to the window as the game's picture is, nearest pixel, in the screen's palette.
// Presentation only.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <vector>
#include <SDL3/SDL.h>
#include "port_hwcursor.h"
#include "grp.h"      // FRAME
#include "img.h"      // _img_list, the cursor's layer
#include "port_console.h"  // PortConsoleHudOn
#include "port_overlay.h"
#include "port_present.h"  // PortPresentPaletteRGB
#include "port_view.h"     // g_portView
#include "../front/port_ui.h"
#include "../../gen/globals117.h"  // gCursorPos_g

// cur.cpp's animation record and its frame counter, bound by name (abs_globals.py): the record's
// pointer is one of the globals kept wide at 64 bits.
extern WORD *sgpCurAnim_g;
extern DWORD sgdwCurFrame_g;

namespace {

// 1.17 places a cursor frame's top left at its offsets less 63 from the pointer (cur_update).
const int kHotOffset = 0x3F;
// The largest system cursor made, in client pixels; past it the scale is lowered.
const int kMostPixels = 256;

// A cursor made, by the frame record it was made from: a screen's cursors are freed and loaded
// again, so the record's address alone could name other art.
struct HwEntry {
  FRAME frame;
  SDL_Cursor *pCursor;
  std::vector<BYTE> used;          // the palette indices it shows
  std::vector<SDL_Color> colours;  // and their colours when it was made
};

bool g_bOn;
bool g_bShown;
float g_fScale;
std::vector<HwEntry> g_entries;
std::vector<SDL_Cursor *> g_retired;  // freed when the system no longer shows them

}  // namespace

// The frame's pixels as palette indices, -1 where it is clear. GRP's codes after the row table:
// 0x80 + n is n clear, 0x40 + n one colour n times, else n colours as given.
void PortGrpFrameDecode(const FRAME &f, std::vector<int> *pPixels)
{
  const int nWidth = f.bWidth;
  const int nHeight = f.bHeight;
  pPixels->assign(static_cast<size_t>(nWidth * nHeight), -1);
  const WORD *pRows = reinterpret_cast<const WORD *>(f.pData);
  for (int y = 0; y < nHeight; ++y) {
    const BYTE *p = f.pData + pRows[y];
    int *pRow = pPixels->data() + y * nWidth;
    int x = 0;
    while (x < nWidth) {
      const int c = *p++;
      if (c == 0)
        break;
      if (c & 0x80) {
        x += c & 0x7F;
      } else if (c & 0x40) {
        const BYTE v = *p++;
        for (int n = c & 0x3F; n > 0 && x < nWidth; --n)
          pRow[x++] = v;
      } else {
        for (int n = 0; n < c && x < nWidth; ++n)
          pRow[x++] = p[n];
        p += c;
      }
    }
  }
}

namespace {

// The box drawn is the frame's and the pointer's pixel together, as a system cursor's hot spot
// must be inside it.
SDL_Cursor *HwMake(const FRAME &f, float fScale, const SDL_Color *pColors, HwEntry *pEntry)
{
  std::vector<int> pixels;
  PortGrpFrameDecode(f, &pixels);
  const int nHotX = kHotOffset - f.bOffsetX;
  const int nHotY = kHotOffset - f._unmapped_01;  // the frame's y offset
  const int nX0 = SDL_min(0, nHotX);
  const int nY0 = SDL_min(0, nHotY);
  const int nBoxW = SDL_max(static_cast<int>(f.bWidth), nHotX + 1) - nX0;
  const int nBoxH = SDL_max(static_cast<int>(f.bHeight), nHotY + 1) - nY0;
  const float s = SDL_min(fScale, static_cast<float>(kMostPixels) / SDL_max(nBoxW, nBoxH));
  const int nW = SDL_max(1, static_cast<int>(SDL_ceilf(nBoxW * s)));
  const int nH = SDL_max(1, static_cast<int>(SDL_ceilf(nBoxH * s)));
  SDL_Surface *pSurface = SDL_CreateSurface(nW, nH, SDL_PIXELFORMAT_RGBA32);
  if (pSurface == NULL)
    return NULL;
  bool abUsed[256] = {};
  for (int y = 0; y < nH; ++y) {
    Uint8 *pOut = static_cast<Uint8 *>(pSurface->pixels) + y * pSurface->pitch;
    const int gy = SDL_min(static_cast<int>(y / s), nBoxH - 1) + nY0;
    for (int x = 0; x < nW; ++x, pOut += 4) {
      const int gx = SDL_min(static_cast<int>(x / s), nBoxW - 1) + nX0;
      const int v = gx >= 0 && gy >= 0 && gx < f.bWidth && gy < f.bHeight ? pixels[gy * f.bWidth + gx] : -1;
      if (v < 0) {
        pOut[0] = pOut[1] = pOut[2] = pOut[3] = 0;
        continue;
      }
      abUsed[v] = true;
      pOut[0] = pColors[v].r;
      pOut[1] = pColors[v].g;
      pOut[2] = pColors[v].b;
      pOut[3] = 255;
    }
  }
  const int nSpotX = SDL_min(static_cast<int>((nHotX - nX0 + 0.5f) * s), nW - 1);
  const int nSpotY = SDL_min(static_cast<int>((nHotY - nY0 + 0.5f) * s), nH - 1);
  SDL_Cursor *pCursor = SDL_CreateColorCursor(pSurface, nSpotX, nSpotY);
  SDL_DestroySurface(pSurface);
  pEntry->used.clear();
  pEntry->colours.clear();
  for (int i = 0; i < 256; ++i)
    if (abUsed[i]) {
      pEntry->used.push_back(static_cast<BYTE>(i));
      pEntry->colours.push_back(pColors[i]);
    }
  return pCursor;
}

bool HwColoursSame(const HwEntry &e, const SDL_Color *pColors)
{
  for (size_t i = 0; i < e.used.size(); ++i) {
    const SDL_Color &a = e.colours[i];
    const SDL_Color &b = pColors[e.used[i]];
    if (a.r != b.r || a.g != b.g || a.b != b.b)
      return false;
  }
  return true;
}

void HwRetire(SDL_Cursor *pCursor)
{
  if (pCursor != NULL)
    g_retired.push_back(pCursor);
}

}  // namespace

void PortHwCursorSet(bool bOn)
{
  g_bOn = bOn;
}

bool PortHwCursorOn(void)
{
  return g_bOn;
}

void PortHwCursorShown(bool bShown)
{
  g_bShown = bShown;
}

bool PortHwCursorShowing(void)
{
  return g_bShown;
}

SDL_Cursor *PortHwCursorNow(float fScale, const SDL_Color *pColors)
{
  SDL_Cursor *const pSystem = SDL_GetCursor();
  for (size_t i = 0; i < g_retired.size();) {
    if (g_retired[i] != pSystem) {
      SDL_DestroyCursor(g_retired[i]);
      g_retired[i] = g_retired.back();
      g_retired.pop_back();
    } else {
      ++i;
    }
  }
  const WORD *const pAnim = sgpCurAnim_g;
  if (pAnim == NULL || *pAnim == 0 || !_img_list[0].bInUse || pColors == NULL)
    return NULL;
  if (SDL_fabsf(fScale - g_fScale) > 0.001f) {
    for (const HwEntry &e : g_entries)
      HwRetire(e.pCursor);
    g_entries.clear();
    g_fScale = fScale;
  }
  const unsigned nFrame = sgdwCurFrame_g % *pAnim;
  const FRAME &f = reinterpret_cast<const FRAME *>(reinterpret_cast<const char *>(pAnim) + 6)[nFrame];
  HwEntry *pEntry = NULL;
  for (HwEntry &e : g_entries)
    if (memcmp(&e.frame, &f, sizeof f) == 0) {
      pEntry = &e;
      break;
    }
  if (pEntry != NULL && pEntry->pCursor != NULL && HwColoursSame(*pEntry, pColors))
    return pEntry->pCursor;
  if (pEntry == NULL) {
    g_entries.push_back(HwEntry());
    pEntry = &g_entries.back();
    pEntry->frame = f;
    pEntry->pCursor = NULL;
  }
  HwRetire(pEntry->pCursor);
  pEntry->pCursor = HwMake(f, fScale, pColors, pEntry);
  return pEntry->pCursor;
}

// ---------------------------------------------------------------------------
// The cursor over a HUD design. The game draws its cursor into its frame, and a HUD design is
// drawn over that frame (port_gpu_hud.cpp), so with one on the overlay draws the cursor last,
// the frame cur.cpp would draw at the place it would draw it, a game pixel a square of the
// game's picture.

namespace {

const int kCursorSlot = 35;  // its picture's slot (port_ui.h)
bool g_bAbove;               // drawn above, as last shown
FRAME g_aboveFrame;          // the frame in the slot, and its colours
unsigned char g_aboveRgb[768];
int g_nAboveW, g_nAboveH;

bool AboveShown(void)
{
  const bool bAbove = PortCursorAbove();
  if (bAbove != g_bAbove) {
    g_bAbove = bAbove;
    img_invalid(0);  // the game draws its cursor again, or stops
  }
  return bAbove;
}

bool AboveDraw(int nWidth, int nHeight, float fX, float fY, float fW, float fH)
{
  (void)nWidth;
  (void)nHeight;
  (void)fH;
  const WORD *const pAnim = sgpCurAnim_g;
  if (pAnim == NULL || *pAnim == 0 || !_img_list[0].bInUse || g_portView.nWidth <= 0 ||
      PortOverlayHas(PORT_OVERLAY_HIDES_CURSOR))
    return false;
  const FRAME &f = reinterpret_cast<const FRAME *>(reinterpret_cast<const char *>(pAnim) + 6)[sgdwCurFrame_g % *pAnim];
  unsigned char rgb[768];
  PortPresentPaletteRGB(rgb);
  if (memcmp(&f, &g_aboveFrame, sizeof f) != 0 || memcmp(rgb, g_aboveRgb, sizeof rgb) != 0) {
    std::vector<int> pixels;
    PortGrpFrameDecode(f, &pixels);
    std::vector<unsigned char> rgba(pixels.size() * 4, 0);
    for (size_t i = 0; i < pixels.size(); ++i) {
      const int v = pixels[i];
      if (v < 0)
        continue;
      rgba[i * 4] = rgb[v * 3];
      rgba[i * 4 + 1] = rgb[v * 3 + 1];
      rgba[i * 4 + 2] = rgb[v * 3 + 2];
      rgba[i * 4 + 3] = 255;
    }
    g_nAboveW = f.bWidth;
    g_nAboveH = f.bHeight;
    if (g_nAboveW > 0 && g_nAboveH > 0)
      UiPictureSet(kCursorSlot, rgba.data(), g_nAboveW, g_nAboveH);
    g_aboveFrame = f;
    memcpy(g_aboveRgb, rgb, sizeof rgb);
  }
  if (g_nAboveW <= 0 || g_nAboveH <= 0)
    return false;
  // its top left at the pointer less 63 and the frame's offsets (cur_update)
  const float k = fW / static_cast<float>(g_portView.nWidth);
  const float x = fX + static_cast<float>(gCursorPos_g[0] - kHotOffset + f.bOffsetX) * k;
  const float y = fY + static_cast<float>(gCursorPos_g[1] - kHotOffset + f._unmapped_01) * k;
  UiPicture(x, y, g_nAboveW * k, g_nAboveH * k, kCursorSlot, UiColor{1.0f, 1.0f, 1.0f, 1.0f}, 0.0f, true);
  return true;
}

const PortOverlayLayer kCursorLayer = {"cursor", PORT_OVERLAY_CURSOR, 0, AboveShown, NULL, AboveDraw,
                                       NULL,     NULL,                NULL, NULL};
PORT_OVERLAY_LAYER(kCursorLayer);

}  // namespace

bool PortCursorAbove(void)
{
  // over a HUD design, and over a layer whose panels are the console's (an add-on's); and
  // under a layer that hides it, where the frame drawn above is none
  return (PortConsoleHudOn() || PortOverlayHas(PORT_OVERLAY_CONSOLE | PORT_OVERLAY_HIDES_CURSOR)) && !g_bShown &&
         gwGameMode == 3;
}

void PortHwCursorFree(void)
{
  for (const HwEntry &e : g_entries)
    if (e.pCursor != NULL)
      SDL_DestroyCursor(e.pCursor);
  g_entries.clear();
  for (SDL_Cursor *pCursor : g_retired)
    SDL_DestroyCursor(pCursor);
  g_retired.clear();
  g_bShown = false;
}
