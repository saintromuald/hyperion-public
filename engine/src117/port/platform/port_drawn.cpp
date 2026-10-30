// port_drawn.cpp -- a small window Hyperion draws itself (port_drawn.h).
#include <windows.h>
#include <math.h>
#include <map>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include "port_drawn.h"
#include "../render/port_maptext.h"  // MapTextFace

namespace {

struct Face {
  stbtt_fontinfo info;
  bool bLoaded;
};

struct Text {
  SDL_Texture *pTexture;
  int w;
  int h;
};

const PortDrawnTheme kLight = {{240, 240, 240, 255}, {20, 20, 20, 255},   {85, 85, 85, 255},    {255, 255, 255, 255},
                               {160, 160, 160, 255}, {0, 103, 192, 255},  {225, 225, 225, 255}, {229, 241, 251, 255},
                               {160, 160, 160, 255}, {0, 100, 0, 255},    {139, 0, 0, 255}};
const PortDrawnTheme kDark = {{32, 32, 32, 255},    {235, 235, 235, 255}, {170, 170, 170, 255}, {45, 45, 45, 255},
                              {100, 100, 100, 255}, {96, 205, 255, 255},  {55, 55, 55, 255},    {65, 78, 90, 255},
                              {110, 110, 110, 255}, {110, 200, 110, 255}, {255, 110, 110, 255}};

bool FaceLoad(Face *pFace, const char *pszResource)
{
  HRSRC hRes = FindResourceA(NULL, pszResource, MAKEINTRESOURCEA(10));  // RT_RCDATA
  HGLOBAL hData = hRes != NULL ? LoadResource(NULL, hRes) : NULL;
  const unsigned char *p = hData != NULL ? static_cast<const unsigned char *>(LockResource(hData)) : NULL;
  pFace->bLoaded = p != NULL && stbtt_InitFont(&pFace->info, p, stbtt_GetFontOffsetForIndex(p, 0)) != 0;
  return pFace->bLoaded;
}

// The font and glyph a character is drawn with: the face's, else, for a script the face lacks
// (Cyrillic, the East Asian ones), a face that has it (MapTextFace), kerned with nothing.
const stbtt_fontinfo *GlyphFont(const Face &face, int nCode, int *pnGlyph)
{
  *pnGlyph = stbtt_FindGlyphIndex(&face.info, nCode);
  if (*pnGlyph == 0 && nCode >= 0x80) {
    int nOther = 0;
    const stbtt_fontinfo *pOther = MapTextFace(static_cast<unsigned>(nCode), &nOther);
    if (pOther != NULL) {
      *pnGlyph = nOther;
      return pOther;
    }
  }
  return &face.info;
}

// A string's width in pixels at a height of fPx pixels.
float Width(const Face &face, float fPx, const std::string &s)
{
  if (!face.bLoaded)
    return 0.0f;
  const float fScale = stbtt_ScaleForPixelHeight(&face.info, fPx);
  const char *p = s.c_str();
  size_t n = s.size();
  float x = 0.0f;
  int nPrev = 0;
  while (n > 0) {
    const int nCode = static_cast<int>(SDL_StepUTF8(&p, &n));
    int nGlyph;
    const stbtt_fontinfo *pFont = GlyphFont(face, nCode, &nGlyph);
    const bool bOwn = pFont == &face.info;
    if (nPrev != 0 && bOwn)
      x += fScale * stbtt_GetGlyphKernAdvance(&face.info, nPrev, nGlyph);
    int nAdvance, nLeft;
    stbtt_GetGlyphHMetrics(pFont, nGlyph, &nAdvance, &nLeft);
    x += (bOwn ? fScale : stbtt_ScaleForPixelHeight(pFont, fPx)) * nAdvance;
    nPrev = bOwn ? nGlyph : 0;
  }
  return x;
}

SDL_FRect Px(const PortDrawn &d, const SDL_FRect &r)
{
  SDL_FRect p = {r.x * d.fScale, r.y * d.fScale, r.w * d.fScale, r.h * d.fScale};
  return p;
}

}  // namespace

struct PortDrawnFonts {
  Face faces[PORT_DRAWN_FACES];
  std::map<std::string, Text> texts;
};

namespace {

// A string drawn white into a texture, its coverage as alpha, once for each face and size.
const Text &TextOf(PortDrawn &d, int nFace, float fPx, const std::string &s)
{
  char szKey[32];
  SDL_snprintf(szKey, sizeof szKey, "%d/%d/", nFace, static_cast<int>(fPx * 10.0f));
  const std::string key = szKey + s;
  std::map<std::string, Text> &texts = d.pFonts->texts;
  std::map<std::string, Text>::iterator it = texts.find(key);
  if (it != texts.end())
    return it->second;
  Text text = {NULL, 0, 0};
  const Face &face = d.pFonts->faces[nFace];
  if (face.bLoaded && !s.empty()) {
    const float fScale = stbtt_ScaleForPixelHeight(&face.info, fPx);
    int nAscent, nDescent, nGap;
    stbtt_GetFontVMetrics(&face.info, &nAscent, &nDescent, &nGap);
    const int nBase = static_cast<int>(ceilf(nAscent * fScale));
    text.h = nBase + static_cast<int>(ceilf(-nDescent * fScale)) + 1;
    text.w = static_cast<int>(ceilf(Width(face, fPx, s))) + 3;
    std::vector<unsigned char> cover(static_cast<size_t>(text.w) * text.h, 0);
    std::vector<unsigned char> glyph;
    const char *p = s.c_str();
    size_t n = s.size();
    float x = 1.0f;
    int nPrev = 0;
    while (n > 0) {
      const int nCode = static_cast<int>(SDL_StepUTF8(&p, &n));
      int nGlyph;
      const stbtt_fontinfo *pFont = GlyphFont(face, nCode, &nGlyph);
      const bool bOwn = pFont == &face.info;
      const float fS = bOwn ? fScale : stbtt_ScaleForPixelHeight(pFont, fPx);
      if (nPrev != 0 && bOwn)
        x += fScale * stbtt_GetGlyphKernAdvance(&face.info, nPrev, nGlyph);
      const float fShift = x - floorf(x);
      int x0, y0, x1, y1;
      MapTextGlyphBox(pFont, nGlyph, fS, fS, fShift, &x0, &y0, &x1, &y1);
      const int gw = x1 - x0, gh = y1 - y0;
      if (gw > 0 && gh > 0) {
        glyph.assign(static_cast<size_t>(gw) * gh, 0);
        MapTextGlyphDraw(pFont, nGlyph, glyph.data(), gw, gh, gw, fS, fS, fShift, x0, y0);
        const int ox = static_cast<int>(floorf(x)) + x0, oy = nBase + y0;
        for (int gy = 0; gy < gh; ++gy)
          for (int gx = 0; gx < gw; ++gx) {
            const int px = ox + gx, py = oy + gy;
            if (px >= 0 && px < text.w && py >= 0 && py < text.h) {
              unsigned char &c = cover[static_cast<size_t>(py) * text.w + px];
              c = SDL_max(c, glyph[static_cast<size_t>(gy) * gw + gx]);
            }
          }
      }
      int nAdvance, nLeft;
      stbtt_GetGlyphHMetrics(pFont, nGlyph, &nAdvance, &nLeft);
      x += fS * nAdvance;
      nPrev = bOwn ? nGlyph : 0;
    }
    SDL_Surface *pSurface = SDL_CreateSurface(text.w, text.h, SDL_PIXELFORMAT_RGBA32);
    if (pSurface != NULL) {
      for (int y = 0; y < text.h; ++y) {
        Uint8 *pRow = static_cast<Uint8 *>(pSurface->pixels) + static_cast<size_t>(y) * pSurface->pitch;
        for (int px = 0; px < text.w; ++px) {
          pRow[px * 4 + 0] = pRow[px * 4 + 1] = pRow[px * 4 + 2] = 255;
          pRow[px * 4 + 3] = cover[static_cast<size_t>(y) * text.w + px];
        }
      }
      text.pTexture = SDL_CreateTextureFromSurface(d.pRenderer, pSurface);
      if (text.pTexture != NULL)
        SDL_SetTextureBlendMode(text.pTexture, SDL_BLENDMODE_BLEND);
      SDL_DestroySurface(pSurface);
    }
  }
  return texts[key] = text;
}

}  // namespace

bool PortDrawnOpen(PortDrawn &d, const char *pszTitle, float fWidth, const char **ppszFailed)
{
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
    *ppszFailed = "SDL's video";
    return false;
  }
  d.fWidth = fWidth;
  d.pTheme = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK ? &kDark : &kLight;
  d.pFonts = new PortDrawnFonts();
  Face *pFaces = d.pFonts->faces;
  FaceLoad(&pFaces[PORT_DRAWN_TEXT], "HYPERION_FONT_TEXT");
  FaceLoad(&pFaces[PORT_DRAWN_BOLD], "HYPERION_FONT_BOLD");
  FaceLoad(&pFaces[PORT_DRAWN_MONO], "HYPERION_FONT_MONO");
  if (!pFaces[PORT_DRAWN_BOLD].bLoaded)
    pFaces[PORT_DRAWN_BOLD] = pFaces[PORT_DRAWN_TEXT];
  if (!pFaces[PORT_DRAWN_MONO].bLoaded)
    pFaces[PORT_DRAWN_MONO] = pFaces[PORT_DRAWN_TEXT];
  // the window's size in its own coordinates, which are the screen's pixels or, on a Mac,
  // its points; drawn in pixels at the screen's scale
  d.pWindow = SDL_CreateWindow(pszTitle, static_cast<int>(fWidth), 480, SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  d.pRenderer = d.pWindow != NULL ? SDL_CreateRenderer(d.pWindow, NULL) : NULL;
  if (d.pRenderer == NULL) {
    *ppszFailed = d.pWindow == NULL ? "its window" : "its renderer";
    PortDrawnClose(d);
    return false;
  }
  d.fScale = SDL_GetWindowDisplayScale(d.pWindow) > 0.0f ? SDL_GetWindowDisplayScale(d.pWindow) : 1.0f;
  SDL_SetRenderVSync(d.pRenderer, 1);
  return true;
}

// HYPERION_DISPLAY's screen, as a test run's window takes it (port_present.cpp), or 0.
static SDL_DisplayID TestDisplay(void)
{
  const char *psz = SDL_getenv("HYPERION_DISPLAY");
  int nIds = 0;
  SDL_DisplayID *pIds = psz != NULL && *psz != '\0' ? SDL_GetDisplays(&nIds) : NULL;
  SDL_DisplayID chosen = 0;
  const bool bLeft = psz != NULL && SDL_strcasecmp(psz, "left") == 0;
  const bool bRight = psz != NULL && SDL_strcasecmp(psz, "right") == 0;
  int nBest = 0;
  for (int i = 0; i < nIds; i++) {
    SDL_Rect r;
    if (!bLeft && !bRight)
      chosen = SDL_atoi(psz) == i + 1 ? pIds[i] : chosen;
    else if (SDL_GetDisplayBounds(pIds[i], &r) && (chosen == 0 || (bLeft ? r.x < nBest : r.x > nBest))) {
      chosen = pIds[i];
      nBest = r.x;
    }
  }
  SDL_free(pIds);
  return chosen;
}

void PortDrawnShow(PortDrawn &d, float fHeight)
{
  const float fDensity = SDL_GetWindowPixelDensity(d.pWindow) > 0.0f ? SDL_GetWindowPixelDensity(d.pWindow) : 1.0f;
  SDL_SetWindowSize(d.pWindow, static_cast<int>(d.fWidth * d.fScale / fDensity),
                    static_cast<int>(ceilf(fHeight * d.fScale / fDensity)));
  const SDL_DisplayID display = TestDisplay();
  if (display != 0)
    SDL_SetWindowPosition(d.pWindow, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
  else
    SDL_SetWindowPosition(d.pWindow, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(d.pWindow);
  SDL_RaiseWindow(d.pWindow);
}

void PortDrawnClose(PortDrawn &d)
{
  if (d.pFonts != NULL) {
    for (std::map<std::string, Text>::iterator it = d.pFonts->texts.begin(); it != d.pFonts->texts.end(); ++it)
      if (it->second.pTexture != NULL)
        SDL_DestroyTexture(it->second.pTexture);
    delete d.pFonts;
    d.pFonts = NULL;
  }
  if (d.pRenderer != NULL)
    SDL_DestroyRenderer(d.pRenderer);
  if (d.pWindow != NULL)
    SDL_DestroyWindow(d.pWindow);
  d.pRenderer = NULL;
  d.pWindow = NULL;
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

float PortDrawnTextWidth(const PortDrawn &d, int nFace, float fSize, const std::string &s)
{
  return Width(d.pFonts->faces[nFace], fSize * d.fScale, s) / d.fScale;
}

float PortDrawnText(PortDrawn &d, int nFace, float fSize, const std::string &s, float x, float y, SDL_Color c,
                    float fClipWidth)
{
  const Text &t = TextOf(d, nFace, fSize * d.fScale, s);
  if (t.pTexture == NULL)
    return 0.0f;
  SDL_SetTextureColorMod(t.pTexture, c.r, c.g, c.b);
  SDL_SetTextureAlphaMod(t.pTexture, c.a);
  SDL_FRect src = {0.0f, 0.0f, static_cast<float>(t.w), static_cast<float>(t.h)};
  const float fClip = fClipWidth * d.fScale;
  if (fClip > 0.0f && src.w > fClip) {
    src.x = src.w - fClip;
    src.w = fClip;
  }
  const SDL_FRect dst = {roundf(x * d.fScale), roundf(y * d.fScale), src.w, src.h};
  SDL_RenderTexture(d.pRenderer, t.pTexture, &src, &dst);
  return src.w / d.fScale;
}

float PortDrawnWrapped(PortDrawn &d, int nFace, float fSize, const std::string &s, float x, float y, float fWidth,
                       SDL_Color c)
{
  const float fLine = fSize * 1.4f;
  float fy = y;
  // each paragraph, at its newline, on lines of its own; an empty one a gap
  for (size_t p = 0; p <= s.size();) {
    size_t q = s.find('\n', p);
    if (q == std::string::npos)
      q = s.size();
    const std::string para = s.substr(p, q - p);
    std::string line;
    size_t i = 0;
    while (i <= para.size()) {
      size_t j = para.find(' ', i);
      if (j == std::string::npos)
        j = para.size();
      std::string word = para.substr(i, j - i);
      // a word wider than the line, such as a long path, broken where it must be
      while (line.empty() && word.size() > 1 && PortDrawnTextWidth(d, nFace, fSize, word) > fWidth) {
        size_t n = word.size();
        while (n > 1 && PortDrawnTextWidth(d, nFace, fSize, word.substr(0, n)) > fWidth)
          for (n--; n > 1 && (static_cast<unsigned char>(word[n]) & 0xC0) == 0x80;)
            n--;
        PortDrawnText(d, nFace, fSize, word.substr(0, n), x, fy, c);
        fy += fLine;
        word.erase(0, n);
      }
      const std::string next = line.empty() ? word : line + " " + word;
      if (!line.empty() && PortDrawnTextWidth(d, nFace, fSize, next) > fWidth) {
        PortDrawnText(d, nFace, fSize, line, x, fy, c);
        fy += fLine;
        line = word;
      } else {
        line = next;
      }
      i = j + 1;
    }
    if (!line.empty())
      PortDrawnText(d, nFace, fSize, line, x, fy, c);
    fy += line.empty() ? fLine * 0.6f : fLine;
    p = q + 1;
  }
  return fy - y;
}

void PortDrawnFill(PortDrawn &d, const SDL_FRect &r, SDL_Color c)
{
  SDL_SetRenderDrawColor(d.pRenderer, c.r, c.g, c.b, c.a);
  const SDL_FRect p = Px(d, r);
  SDL_RenderFillRect(d.pRenderer, &p);
}

void PortDrawnOutline(PortDrawn &d, const SDL_FRect &r, SDL_Color c, float fThick)
{
  PortDrawnFill(d, {r.x, r.y, r.w, fThick}, c);
  PortDrawnFill(d, {r.x, r.y + r.h - fThick, r.w, fThick}, c);
  PortDrawnFill(d, {r.x, r.y, fThick, r.h}, c);
  PortDrawnFill(d, {r.x + r.w - fThick, r.y, fThick, r.h}, c);
}

void PortDrawnDisc(PortDrawn &d, float cx, float cy, float fRadius, SDL_Color c)
{
  SDL_SetRenderDrawColor(d.pRenderer, c.r, c.g, c.b, c.a);
  const float r = fRadius * d.fScale, x0 = cx * d.fScale, y0 = cy * d.fScale;
  for (float dy = -r; dy <= r; dy += 1.0f) {
    const float dx = sqrtf(SDL_max(0.0f, r * r - dy * dy));
    SDL_RenderLine(d.pRenderer, x0 - dx, y0 + dy, x0 + dx, y0 + dy);
  }
}

void PortDrawnClip(PortDrawn &d, const SDL_FRect *r)
{
  if (r == NULL) {
    SDL_SetRenderClipRect(d.pRenderer, NULL);
    return;
  }
  const SDL_FRect p = Px(d, *r);
  const SDL_Rect clip = {static_cast<int>(p.x), static_cast<int>(p.y), static_cast<int>(ceilf(p.w)),
                         static_cast<int>(ceilf(p.h))};
  SDL_SetRenderClipRect(d.pRenderer, &clip);
}

bool PortDrawnInside(const SDL_FRect &r, float x, float y)
{
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void PortDrawnButton(PortDrawn &d, const SDL_FRect &r, const char *pszLabel, bool bEnabled, bool bDefault)
{
  const PortDrawnTheme &t = *d.pTheme;
  const bool bOver = bEnabled && PortDrawnInside(r, d.fMouseX, d.fMouseY);
  PortDrawnFill(d, r, bOver ? t.hover : t.button);
  PortDrawnOutline(d, r, bDefault && bEnabled ? t.accent : t.line, bDefault && bEnabled ? 2.0f : 1.0f);
  const float fSize = 16.0f;
  const float fText = PortDrawnTextWidth(d, PORT_DRAWN_TEXT, fSize, pszLabel);
  PortDrawnText(d, PORT_DRAWN_TEXT, fSize, pszLabel, r.x + (r.w - fText) / 2.0f, r.y + (r.h - fSize * 1.35f) / 2.0f,
                bEnabled ? t.text : t.disabled);
}

void PortDrawnEvent(PortDrawn &d, SDL_Event &e)
{
  SDL_ConvertEventToRenderCoordinates(d.pRenderer, &e);
  if (e.type == SDL_EVENT_MOUSE_MOTION) {
    d.fMouseX = e.motion.x / d.fScale;
    d.fMouseY = e.motion.y / d.fScale;
  }
}
