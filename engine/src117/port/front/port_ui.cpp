// port_ui.cpp -- the front end's drawing layer (docs/modules/front-end.md, "Layout and
// drawing"; docs/front-end.md, F1). Presentation only.
//
// The front end draws at the window's own resolution, not into the game's 640x480 frame. Its
// screens lay out with Clay (zlib, port_clay.cpp) in pixels: UiScale turns the design's stage,
// 1080 units high, into the target's pixels, so text is rasterized at the size it is shown.
// Clay's commands become primitives here (UiPrim), which port_gpu.cpp draws in one pass
// (shaders/ui.hlsl). Text is Saira, titles Michroma, the score's numbers IBM Plex Mono, all
// OFL, carried in Hyperion.exe as resources (tools/fetch_fonts.py, the build's fonts.rc);
// HYPERION_FONT, _WIDE, _BOLD and _MONO name files in their place, and without the resources
// Windows' Bahnschrift (Consolas for the numbers) stands in. stb_truetype (public domain)
// rasterizes each glyph once, at the size it is drawn, into one atlas.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <SDL3/SDL.h>
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"
#include "clay.h"
#include "port_ui.h"
#include "../platform/port_posix.h"  // PortNativePath
#include "port_frontscreens.h"  // FrontLog
#include "../render/port_maptext.h"  // MapTextFace

namespace {

const int kAtlasSize = 4096;
const float kStageHeight = 1080.0f;
const float kStageMinWidth = 1920.0f;  // the design's 16:9, which its nav bar needs across

struct Glyph {
  float u0, v0, u1, v1;
  int nWidth, nHeight;
  int nOffsetX, nOffsetY;  // from the pen on the baseline to the bitmap's top left
  int nIndex;              // the font's glyph, for kerning
  float fAdvance;
  bool bInAtlas;
};

struct Face {
  stbtt_fontinfo font;
  void *pData;
  bool bResource;  // pData is the exe's, not to be freed
  bool bLoaded;
  char szSource[96];
};

Face g_faces[UI_FACES];
bool g_bFacesTried;
std::unordered_map<unsigned long long, Glyph> g_glyphs;
std::vector<unsigned char> g_atlas;
int g_nShelfX;
int g_nShelfY;
int g_nShelfH;
int g_nDirtyFirst = -1;
int g_nDirtyLast = -1;
bool g_bAtlasFull;

// The picture atlas: UI_PICTURE_SLOTS slots of UI_PICTURE_SIDE squared, four across.
const int kPictureAcross = UI_PICTURE_ACROSS;
const int kPictureAtlasW = UI_PICTURE_SIDE * kPictureAcross;
const int kPictureAtlasH = UI_PICTURE_SIDE * (UI_PICTURE_SLOTS / kPictureAcross);
std::vector<unsigned char> g_pictures;
int g_pictureSize[UI_PICTURE_SLOTS][2];
int g_nPicturesFirst = -1;
int g_nPicturesLast = -1;

std::vector<UiPrim> g_prims;
UiColor g_glow = {0.0f, 120 / 255.0f, 1.0f, 1.0f};  // UiSetGlow
std::vector<float> g_clips;  // x0, y0, x1, y1 each
int g_nWidth;
int g_nHeight;
float g_fScale = 1.0f;
float g_turn[3];

// Clay's arena and context.
void *g_pClayMemory;
Clay_Context *g_pClay;

void *FaceFile(const char *pszPath)
{
  size_t nSize = 0;
  return SDL_LoadFile(PortNativePath(pszPath), &nSize);
}

void FacesLoad(void)
{
  if (g_bFacesTried)
    return;
  g_bFacesTried = true;
  static const char *const kResource[UI_FACES] = {"HYPERION_FONT_TEXT",       "HYPERION_FONT_WIDE",
                                                 "HYPERION_FONT_BOLD",       "HYPERION_FONT_MONO",
                                                 "HYPERION_FONT_PETCH",      "HYPERION_FONT_PETCH_MEDIUM",
                                                 "HYPERION_FONT_PETCH_SEMI", "HYPERION_FONT_PETCH_BOLD"};
  static const char *const kEnv[UI_FACES] = {"HYPERION_FONT",       "HYPERION_FONT_WIDE",
                                            "HYPERION_FONT_BOLD",  "HYPERION_FONT_MONO",
                                            "HYPERION_FONT_PETCH", "HYPERION_FONT_PETCH_MEDIUM",
                                            "HYPERION_FONT_PETCH_SEMI", "HYPERION_FONT_PETCH_BOLD"};
  char szFonts[MAX_PATH];
  GetWindowsDirectoryA(szFonts, sizeof szFonts);
  for (int i = 0; i < UI_FACES; ++i) {
    Face &f = g_faces[i];
    const char *pszEnv = SDL_getenv(kEnv[i]);
    if (pszEnv == NULL && i != UI_FACE_TEXT && i != UI_FACE_MONO)
      pszEnv = SDL_getenv("HYPERION_FONT");
    if (pszEnv != NULL && pszEnv[0] != 0 && (f.pData = FaceFile(pszEnv)) != NULL) {
      SDL_snprintf(f.szSource, sizeof f.szSource, "file %s", pszEnv);
    } else {
      // the exe's own: resources stay mapped for the process's life, so no copy is made
      HRSRC hRes = FindResourceA(NULL, kResource[i], MAKEINTRESOURCEA(10));  // RT_RCDATA
      HGLOBAL hData = hRes != NULL ? LoadResource(NULL, hRes) : NULL;
      if (hData != NULL && (f.pData = LockResource(hData)) != NULL) {
        f.bResource = true;
        SDL_snprintf(f.szSource, sizeof f.szSource, "Hyperion's %s", kResource[i]);
      } else {
        char szPath[MAX_PATH + 32];
        SDL_snprintf(szPath, sizeof szPath, "%s\\Fonts\\%s", szFonts,
                     i == UI_FACE_MONO ? "consola.ttf" : "bahnschrift.ttf");
        if ((f.pData = FaceFile(szPath)) == NULL) {
          SDL_snprintf(szPath, sizeof szPath, "%s\\Fonts\\segoeui.ttf", szFonts);
          f.pData = FaceFile(szPath);
        }
        SDL_snprintf(f.szSource, sizeof f.szSource, "system %s", szPath);
      }
    }
    const unsigned char *p = static_cast<const unsigned char *>(f.pData);
    f.bLoaded = p != NULL && stbtt_InitFont(&f.font, p, stbtt_GetFontOffsetForIndex(p, 0)) != 0;
  }
}

// A face by its number, the text face for one Hyperion does not have: Clay carries a
// screen's fontId as a number of its own and a screen may name a face Hyperion does not have.
UiFace FaceNumber(UiFace face)
{
  return static_cast<unsigned int>(face) < static_cast<unsigned int>(UI_FACES) ? face : UI_FACE_TEXT;
}

Face *FaceOf(UiFace face)
{
  FacesLoad();
  Face *f = &g_faces[FaceNumber(face)];
  if (!f->bLoaded)
    f = &g_faces[UI_FACE_TEXT];
  return f->bLoaded ? f : NULL;
}

// The next UTF-8 character in [*pp, pEnd), or 0 at the end.
unsigned int NextChar(const char **pp, const char *pEnd)
{
  const unsigned char *p = reinterpret_cast<const unsigned char *>(*pp);
  const unsigned char *e = reinterpret_cast<const unsigned char *>(pEnd);
  if (p >= e)
    return 0;
  unsigned int c = *p++;
  int nMore = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
  if (nMore > 0)
    c &= 0x3F >> nMore;
  while (nMore-- > 0 && p < e && (*p & 0xC0) == 0x80)
    c = (c << 6) | (*p++ & 0x3F);
  *pp = reinterpret_cast<const char *>(p);
  return c;
}

// The glyph for c at a whole pixel size, rasterized into the atlas on first use.
const Glyph *GlyphOf(UiFace face, int nSize, unsigned int c)
{
  const unsigned long long nKey =
      (static_cast<unsigned long long>(face) << 56) | (static_cast<unsigned long long>(nSize) << 32) | c;
  auto it = g_glyphs.find(nKey);
  if (it != g_glyphs.end())
    return &it->second;
  Face *f = FaceOf(face);
  if (f == NULL)
    return NULL;
  if (g_atlas.empty())
    g_atlas.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
  Glyph g;
  SDL_zero(g);
  g.nIndex = stbtt_FindGlyphIndex(&f->font, static_cast<int>(c));
  // a character the face lacks, a map's Hangul among them, from a face that has it
  // (port_maptext.h), kerned with nothing as another face's glyph
  const stbtt_fontinfo *pFont = &f->font;
  int nGlyph = g.nIndex;
  if (nGlyph == 0 && c >= 0x80) {
    const stbtt_fontinfo *pOther = MapTextFace(c, &nGlyph);
    if (pOther != NULL)
      pFont = pOther;
    else
      nGlyph = 0;
  }
  const float fScale = stbtt_ScaleForMappingEmToPixels(pFont, static_cast<float>(nSize));
  int nAdvance = 0;
  int nBearing = 0;
  stbtt_GetGlyphHMetrics(pFont, nGlyph, &nAdvance, &nBearing);
  g.fAdvance = nAdvance * fScale;
  int x0, y0, x1, y1;
  MapTextGlyphBox(pFont, nGlyph, fScale, fScale, 0.0f, &x0, &y0, &x1, &y1);
  g.nWidth = x1 - x0;
  g.nHeight = y1 - y0;
  g.nOffsetX = x0;
  g.nOffsetY = y0;
  if (g.nWidth > 0 && g.nHeight > 0) {
    // shelves, a pixel apart
    if (g_nShelfX + g.nWidth + 1 > kAtlasSize) {
      g_nShelfY += g_nShelfH + 1;
      g_nShelfX = 0;
      g_nShelfH = 0;
    }
    if (g_nShelfY + g.nHeight + 1 > kAtlasSize) {
      // the shelves are full, as a language with thousands of characters can fill them: this
      // glyph is not drawn this frame, and the next frame starts the atlas over (UiBegin)
      if (!g_bAtlasFull) {
        g_bAtlasFull = true;
        SDL_Log("ui: the glyph atlas is full; it starts over at the next frame");
        FrontKit::FrontLog("ui: the glyph atlas is full; it starts over at the next frame\n");
      }
    } else {
      unsigned char *pAt = &g_atlas[static_cast<size_t>(g_nShelfY) * kAtlasSize + g_nShelfX];
      MapTextGlyphDraw(pFont, nGlyph, pAt, g.nWidth, g.nHeight, kAtlasSize, fScale, fScale, 0.0f, x0, y0);
      g.u0 = static_cast<float>(g_nShelfX) / kAtlasSize;
      g.v0 = static_cast<float>(g_nShelfY) / kAtlasSize;
      g.u1 = static_cast<float>(g_nShelfX + g.nWidth) / kAtlasSize;
      g.v1 = static_cast<float>(g_nShelfY + g.nHeight) / kAtlasSize;
      g.bInAtlas = true;
      if (g_nDirtyFirst < 0 || g_nShelfY < g_nDirtyFirst)
        g_nDirtyFirst = g_nShelfY;
      if (g_nShelfY + g.nHeight > g_nDirtyLast)
        g_nDirtyLast = g_nShelfY + g.nHeight;
      g_nShelfX += g.nWidth + 1;
      if (g.nHeight > g_nShelfH)
        g_nShelfH = g.nHeight;
    }
  }
  return &g_glyphs.emplace(nKey, g).first->second;
}

float Kern(UiFace face, int nSize, int nPrev, int nNext)
{
  Face *f = FaceOf(face);
  if (f == NULL || nPrev == 0 || nNext == 0)
    return 0.0f;
  return stbtt_GetGlyphKernAdvance(&f->font, nPrev, nNext) *
         stbtt_ScaleForMappingEmToPixels(&f->font, static_cast<float>(nSize));
}

UiPrim *Push(float x, float y, float w, float h, UiColor c, UiPrimKind kind)
{
  g_prims.emplace_back();
  UiPrim &p = g_prims.back();
  SDL_zero(p);
  p.rect[0] = x;
  p.rect[1] = y;
  p.rect[2] = w;
  p.rect[3] = h;
  p.color[0] = c.r;
  p.color[1] = c.g;
  p.color[2] = c.b;
  p.color[3] = c.a;
  const float *pClip = g_clips.empty() ? NULL : &g_clips[g_clips.size() - 4];
  p.clip[0] = pClip != NULL ? pClip[0] : 0.0f;
  p.clip[1] = pClip != NULL ? pClip[1] : 0.0f;
  p.clip[2] = pClip != NULL ? pClip[2] : static_cast<float>(g_nWidth);
  p.clip[3] = pClip != NULL ? pClip[3] : static_cast<float>(g_nHeight);
  p.shape[2] = static_cast<float>(kind);
  if (kind != UI_GLYPH && kind != UI_PICTURE && g_turn[0] != 0.0f) {
    p.uv[0] = g_turn[0];
    p.uv[1] = g_turn[1];
    p.uv[2] = g_turn[2];
  }
  return &p;
}

UiColor ClayColor(Clay_Color c)
{
  UiColor u = {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
  return u;
}

Clay_Dimensions ClayMeasure(Clay_StringSlice text, Clay_TextElementConfig *pConfig, void *)
{
  const UiFace face = static_cast<UiFace>(pConfig->fontId);
  const float fSize = static_cast<float>(pConfig->fontSize);
  Clay_Dimensions d;
  d.width = UiTextWidth(face, fSize, pConfig->letterSpacing, text.chars, text.length);
  if (pConfig->lineHeight > 0) {
    d.height = pConfig->lineHeight;
  } else {
    float fAscent, fDescent, fGap;
    UiFontMetrics(face, fSize, &fAscent, &fDescent, &fGap);
    d.height = ceilf(fAscent - fDescent + fGap);
  }
  return d;
}

void ClayError(Clay_ErrorData error)
{
  SDL_Log("clay: %.*s", static_cast<int>(error.errorText.length), error.errorText.chars);
  FrontKit::FrontLog("clay: %.*s\n", static_cast<int>(error.errorText.length), error.errorText.chars);
}

struct Motion {
  unsigned int nId;
  UiMotion m;
  Clay_BoundingBox box;
  bool bFound;
};
std::vector<Motion> g_motions;

// The motion a draw at b takes: the smallest element with one that holds its centre.
const Motion *MotionAt(const Clay_BoundingBox &b)
{
  const float cx = b.x + b.width * 0.5f, cy = b.y + b.height * 0.5f;
  const Motion *pBest = NULL;
  for (const Motion &mo : g_motions) {
    const Clay_BoundingBox &r = mo.box;
    if (!mo.bFound || cx < r.x || cy < r.y || cx > r.x + r.width || cy > r.y + r.height)
      continue;
    if (pBest == NULL || r.width * r.height < pBest->box.width * pBest->box.height)
      pBest = &mo;
  }
  return pBest;
}

// The primitives from nFirst on, as the motion draws them.
void MotionApply(const Motion &mo, size_t nFirst)
{
  const UiMotion &m = mo.m;
  const float cx = mo.box.x + mo.box.width * 0.5f, cy = mo.box.y + mo.box.height * 0.5f;
  const float s = m.fScale;
  const auto X = [&](float x) { return cx + (x - cx) * s + m.fDx; };
  const auto Y = [&](float y) { return cy + (y - cy) * s + m.fDy; };
  for (size_t i = nFirst; i < g_prims.size(); ++i) {
    UiPrim &p = g_prims[i];
    p.color[3] *= m.fAlpha;
    p.color2[3] *= m.fAlpha;
    p.rect[0] = X(p.rect[0]);
    p.rect[1] = Y(p.rect[1]);
    p.rect[2] *= s;
    p.rect[3] *= s;
    const UiPrimKind kind = static_cast<UiPrimKind>(static_cast<int>(p.shape[2]));
    if (kind != UI_GLYPH && kind != UI_PICTURE) {
      p.shape[0] *= s;
      p.shape[1] *= s;
      if (p.uv[0] != 0.0f) {
        p.uv[1] = X(p.uv[1]);
        p.uv[2] = Y(p.uv[2]);
      }
    }
    // a clip of the element's own moves with it; one from outside it stays
    const Clay_BoundingBox &r = mo.box;
    if (p.clip[0] >= r.x - 1.0f && p.clip[1] >= r.y - 1.0f && p.clip[2] <= r.x + r.width + 1.0f &&
        p.clip[3] <= r.y + r.height + 1.0f) {
      p.clip[0] = X(p.clip[0]);
      p.clip[1] = Y(p.clip[1]);
      p.clip[2] = X(p.clip[2]);
      p.clip[3] = Y(p.clip[3]);
    }
  }
}

void ClayDraw(Clay_RenderCommandArray commands)
{
  for (Motion &mo : g_motions) {
    Clay_ElementId id;
    SDL_zero(id);
    id.id = mo.nId;
    const Clay_ElementData data = Clay_GetElementData(id);
    mo.bFound = data.found;
    mo.box = data.boundingBox;
  }
  for (int32_t i = 0; i < commands.length; ++i) {
    const Clay_RenderCommand *pCmd = Clay_RenderCommandArray_Get(&commands, i);
    const Clay_BoundingBox b = pCmd->boundingBox;
    const Motion *pMotion = g_motions.empty() ? NULL : MotionAt(b);
    const size_t nFirst = g_prims.size();
    switch (pCmd->commandType) {
      case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
        const Clay_RectangleRenderData &r = pCmd->renderData.rectangle;
        UiFill(b.x, b.y, b.width, b.height, ClayColor(r.backgroundColor), r.cornerRadius.topLeft);
        break;
      }
      case CLAY_RENDER_COMMAND_TYPE_BORDER: {
        const Clay_BorderRenderData &r = pCmd->renderData.border;
        const UiColor c = ClayColor(r.color);
        const Clay_BorderWidth w = r.width;
        if (r.cornerRadius.topLeft > 0.0f && w.left == w.right && w.left == w.top && w.left == w.bottom) {
          UiRing(b.x, b.y, b.width, b.height, c, r.cornerRadius.topLeft, w.left);
        } else {
          if (w.top > 0)
            UiFill(b.x, b.y, b.width, w.top, c);
          if (w.bottom > 0)
            UiFill(b.x, b.y + b.height - w.bottom, b.width, w.bottom, c);
          if (w.left > 0)
            UiFill(b.x, b.y + w.top, w.left, b.height - w.top - w.bottom, c);
          if (w.right > 0)
            UiFill(b.x + b.width - w.right, b.y + w.top, w.right, b.height - w.top - w.bottom, c);
        }
        break;
      }
      case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        const Clay_TextRenderData &t = pCmd->renderData.text;
        const UiFace face = static_cast<UiFace>(t.fontId);
        float fAscent, fDescent, fGap;
        UiFontMetrics(face, t.fontSize, &fAscent, &fDescent, &fGap);
        const float fBaseline = b.y + (b.height - (fAscent - fDescent)) * 0.5f + fAscent;
        const int nUserData = static_cast<int>(reinterpret_cast<intptr_t>(pCmd->userData));
        const int nUnderline = (nUserData & 0xFFFF) - 1;
        if (nUserData & UI_TEXT_GLOW) {
          // a halo: the text in the glow colour, faint, twelve times round its place
          const float r = fmaxf(1.0f, t.fontSize * 0.09f);
          UiColor g = g_glow;
          g.a *= ClayColor(t.textColor).a;
          for (int k = 0; k < 12; ++k) {
            const float a = k * 0.5235988f, rr = (k & 1) ? r : r * 0.5f;
            g.a = g_glow.a * ClayColor(t.textColor).a * ((k & 1) ? 0.11f : 0.2f);
            UiText(b.x + cosf(a) * rr, fBaseline + sinf(a) * rr, face, t.fontSize, t.letterSpacing, g,
                   t.stringContents.chars, t.stringContents.length, -1);
          }
        }
        UiText(b.x, fBaseline, face, t.fontSize, t.letterSpacing, ClayColor(t.textColor),
               t.stringContents.chars, t.stringContents.length, nUnderline);
        break;
      }
      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        UiPushClip(b.x, b.y, b.x + b.width, b.y + b.height);
        break;
      case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        UiPopClip();
        break;
      case CLAY_RENDER_COMMAND_TYPE_CUSTOM: {
        const Clay_CustomRenderData &r = pCmd->renderData.custom;
        const UiCustom *pCustom = static_cast<const UiCustom *>(r.customData);
        if (pCustom != NULL && pCustom->fBlur > 0.0f)
          UiBlur(b.x, b.y, b.width, b.height, r.cornerRadius.topLeft, pCustom->fBlur);
        if (r.backgroundColor.a > 0.0f)
          UiFill(b.x, b.y, b.width, b.height, ClayColor(r.backgroundColor), r.cornerRadius.topLeft);
        if (pCustom != NULL && pCustom->pfnDraw != NULL)
          pCustom->pfnDraw(b.x, b.y, b.width, b.height, pCustom->pCtx);
        break;
      }
      default:
        break;
    }
    if (pMotion != NULL)
      MotionApply(*pMotion, nFirst);
  }
}

}  // namespace

UiColor UiRgb(unsigned int nRgb, float fAlpha)
{
  UiColor c = {((nRgb >> 16) & 0xFF) / 255.0f, ((nRgb >> 8) & 0xFF) / 255.0f, (nRgb & 0xFF) / 255.0f, fAlpha};
  return c;
}

void UiBegin(int nWidth, int nHeight)
{
  g_nWidth = nWidth;
  g_nHeight = nHeight;
  // a window narrower than the stage's 16:9 (4:3, as a game without --view leaves it) takes
  // the scale from its width, so the nav bar fits and the height to spare is below it
  g_fScale = SDL_min(nHeight / kStageHeight, nWidth / kStageMinWidth);
  g_prims.clear();
  g_clips.clear();
  g_turn[0] = 0.0f;
  if (g_bAtlasFull) {
    // the glyphs this frame needs are drawn again as it asks for them
    g_bAtlasFull = false;
    g_glyphs.clear();
    SDL_memset(g_atlas.data(), 0, g_atlas.size());
    g_nShelfX = g_nShelfY = g_nShelfH = 0;
    g_nDirtyFirst = 0;
    g_nDirtyLast = kAtlasSize;
  }
}

float UiScale(void) { return g_fScale; }
float UiPx(float fUnits) { return fUnits * g_fScale; }
int UiWidth(void) { return g_nWidth; }
int UiHeight(void) { return g_nHeight; }

void UiFill(float x, float y, float w, float h, UiColor c, float fRadius)
{
  if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f)
    return;
  Push(x, y, w, h, c, UI_FILL)->shape[0] = fRadius;
}

void UiGradient(float x, float y, float w, float h, UiColor c, UiColor c2, bool bAcross, float fRadius)
{
  if (w <= 0.0f || h <= 0.0f)
    return;
  UiPrim *p = Push(x, y, w, h, c, UI_FILL);
  p->shape[0] = fRadius;
  p->color2[0] = c2.r;
  p->color2[1] = c2.g;
  p->color2[2] = c2.b;
  p->color2[3] = c2.a;
  p->shape[3] = bAcross ? 2.0f : 1.0f;
}

void UiGradientMid(float x, float y, float w, float h, UiColor c, UiColor c2)
{
  UiGradient(x, y, w, h, c, c2, true);
  if (!g_prims.empty() && w > 0.0f && h > 0.0f)
    g_prims.back().shape[3] = 3.0f;
}

void UiRing(float x, float y, float w, float h, UiColor c, float fRadius, float fWidth)
{
  if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f || fWidth <= 0.0f)
    return;
  UiPrim *p = Push(x, y, w, h, c, UI_RING);
  p->shape[0] = fRadius;
  p->shape[1] = fWidth;
}

void UiPictureSet(int nSlot, const unsigned char *pRgba, int nWidth, int nHeight)
{
  if (nSlot < 0 || nSlot >= UI_PICTURE_SLOTS || pRgba == NULL || nWidth <= 0 || nHeight <= 0)
    return;
  if (g_pictures.empty())
    g_pictures.assign(static_cast<size_t>(kPictureAtlasW) * kPictureAtlasH * 4, 0);
  // as wide as the slots from this one to the row's end
  const int nRowLeft = (kPictureAcross - nSlot % kPictureAcross) * UI_PICTURE_SIDE;
  const int nPitch = nWidth;
  nWidth = SDL_min(nWidth, nRowLeft);
  nHeight = SDL_min(nHeight, static_cast<int>(UI_PICTURE_SIDE));
  const int x0 = (nSlot % kPictureAcross) * UI_PICTURE_SIDE, y0 = (nSlot / kPictureAcross) * UI_PICTURE_SIDE;
  for (int y = 0; y < nHeight; ++y)
    SDL_memcpy(&g_pictures[(static_cast<size_t>(y0 + y) * kPictureAtlasW + x0) * 4],
               pRgba + static_cast<size_t>(y) * nPitch * 4, static_cast<size_t>(nWidth) * 4);
  g_pictureSize[nSlot][0] = nWidth;
  g_pictureSize[nSlot][1] = nHeight;
  g_nPicturesFirst = g_nPicturesFirst < 0 ? y0 : SDL_min(g_nPicturesFirst, y0);
  g_nPicturesLast = SDL_max(g_nPicturesLast, y0 + nHeight);
}

void UiPicture(float x, float y, float w, float h, int nSlot, UiColor c, float fRadius, bool bNearest)
{
  if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f || nSlot < 0 || nSlot >= UI_PICTURE_SLOTS || g_pictureSize[nSlot][0] == 0)
    return;
  UiPrim *p = Push(x, y, w, h, c, UI_PICTURE);
  p->shape[0] = fRadius;
  p->shape[1] = bNearest ? 1.0f : 0.0f;
  // from the first texel's centre to the last's, so a filtered edge never reaches the next slot
  const float x0 = static_cast<float>((nSlot % kPictureAcross) * UI_PICTURE_SIDE);
  const float y0 = static_cast<float>((nSlot / kPictureAcross) * UI_PICTURE_SIDE);
  p->uv[0] = (x0 + 0.5f) / kPictureAtlasW;
  p->uv[1] = (y0 + 0.5f) / kPictureAtlasH;
  p->uv[2] = (x0 + g_pictureSize[nSlot][0] - 0.5f) / kPictureAtlasW;
  p->uv[3] = (y0 + g_pictureSize[nSlot][1] - 0.5f) / kPictureAtlasH;
}

void UiPicturePart(float x, float y, float w, float h, int nSlot, float u0, float v0, float u1, float v1, UiColor c,
                   bool bNearest)
{
  if (w <= 0.0f || h <= 0.0f || c.a <= 0.0f || nSlot < 0 || nSlot >= UI_PICTURE_SLOTS || g_pictureSize[nSlot][0] == 0)
    return;
  UiPrim *p = Push(x, y, w, h, c, UI_PICTURE);
  p->shape[0] = 0.0f;
  p->shape[1] = bNearest ? 1.0f : 0.0f;
  const float x0 = static_cast<float>((nSlot % kPictureAcross) * UI_PICTURE_SIDE);
  const float y0 = static_cast<float>((nSlot / kPictureAcross) * UI_PICTURE_SIDE);
  p->uv[0] = (x0 + u0 + 0.5f) / kPictureAtlasW;
  p->uv[1] = (y0 + v0 + 0.5f) / kPictureAtlasH;
  p->uv[2] = (x0 + u1 - 0.5f) / kPictureAtlasW;
  p->uv[3] = (y0 + v1 - 0.5f) / kPictureAtlasH;
}

bool UiTextCovered(UiFace face, const char *pszUtf8, size_t nBytes)
{
  Face *f = FaceOf(face);
  if (f == NULL)
    return false;
  const char *p = pszUtf8, *pEnd = pszUtf8 + nBytes;
  while (p < pEnd) {
    const unsigned int c = NextChar(&p, pEnd);
    if (c > 0x20 && c != 0xFEFF && stbtt_FindGlyphIndex(&f->font, static_cast<int>(c)) == 0)
      return false;
  }
  return true;
}

std::string UiUpper(const char *pszUtf8)
{
  std::string out;
  const char *p = pszUtf8, *pEnd = pszUtf8 + strlen(pszUtf8);
  while (p < pEnd) {
    const char *pAt = p;
    const unsigned int c = NextChar(&p, pEnd);
    unsigned int u = c;
    if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7) || (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) ||
        (c >= 0x430 && c <= 0x44F))
      u = c - 0x20;
    else if (c == 0xDF) {
      out += "SS";
      continue;
    } else if (c == 0xFF)
      u = 0x178;
    else if (c == 0x131)
      u = 'I';
    else if (c == 0x17F)
      u = 'S';
    else if (c == 0x3C2)
      u = 0x3A3;
    else if (c >= 0x450 && c <= 0x45F)
      u = c - 0x50;
    else if (c >= 0x100 && c <= 0x17E && c != 0x138 && c != 0x149 && c != 0x178) {
      // Latin Extended-A's pairs, the capital first, but in two runs where it is second
      const bool bOddCapital = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);
      if ((c % 2 == 0) == bOddCapital)
        u = c - 1;
    }
    if (u == c) {
      out.append(pAt, static_cast<size_t>(p - pAt));
      continue;
    }
    char sz[5];
    const char *pszEnd = SDL_UCS4ToUTF8(u, sz);
    out.append(sz, static_cast<size_t>(pszEnd - sz));
  }
  return out;
}

bool UiGlyphCover(UiFace face, float fSize, unsigned int c, std::vector<unsigned char> *pCover, int *pnWidth,
                  int *pnHeight)
{
  Face *f = FaceOf(face);
  if (f == NULL)
    return false;
  const float fScale = stbtt_ScaleForMappingEmToPixels(&f->font, fSize);
  const int nIndex = stbtt_FindGlyphIndex(&f->font, static_cast<int>(c));
  int x0, y0, x1, y1;
  stbtt_GetGlyphBitmapBox(&f->font, nIndex, fScale, fScale, &x0, &y0, &x1, &y1);
  if (nIndex == 0 || x1 <= x0 || y1 <= y0)
    return false;
  *pnWidth = x1 - x0;
  *pnHeight = y1 - y0;
  pCover->assign(static_cast<size_t>(*pnWidth) * *pnHeight, 0);
  stbtt_MakeGlyphBitmap(&f->font, pCover->data(), *pnWidth, *pnHeight, *pnWidth, fScale, fScale, nIndex);
  return true;
}

void UiPlanet(float x, float y, float w, float h)
{
  const UiColor c = {1, 1, 1, 1};
  Push(x, y, w, h, c, UI_PLANET);
}

void UiBlur(float x, float y, float w, float h, float fRadius, float fBlur)
{
  const UiColor c = {1, 1, 1, 1};
  UiPrim *p = Push(x, y, w, h, c, UI_BLUR);
  p->shape[0] = fRadius;
  p->shape[1] = fBlur;
}

void UiLine(float x0, float y0, float x1, float y1, UiColor c, float fWidth)
{
  if (y0 == y1) {
    UiFill(fminf(x0, x1), y0 - fWidth * 0.5f, fabsf(x1 - x0), fWidth, c);
  } else if (x0 == x1) {
    UiFill(x0 - fWidth * 0.5f, fminf(y0, y1), fWidth, fabsf(y1 - y0), c);
  } else {
    const float turn[3] = {g_turn[0], g_turn[1], g_turn[2]};
    const float mx = (x0 + x1) * 0.5f;
    const float my = (y0 + y1) * 0.5f;
    const float len = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
    UiSetTurn(atan2f(y1 - y0, x1 - x0), mx, my);
    UiFill(mx - len * 0.5f, my - fWidth * 0.5f, len, fWidth, c);
    UiSetTurn(turn[0], turn[1], turn[2]);
  }
}

void UiSetTurn(float fAngle, float x, float y)
{
  g_turn[0] = fAngle;
  g_turn[1] = x;
  g_turn[2] = y;
}

void UiPushClip(float x0, float y0, float x1, float y1)
{
  if (!g_clips.empty()) {
    const float *p = &g_clips[g_clips.size() - 4];
    x0 = fmaxf(x0, p[0]);
    y0 = fmaxf(y0, p[1]);
    x1 = fminf(x1, p[2]);
    y1 = fminf(y1, p[3]);
  }
  g_clips.push_back(x0);
  g_clips.push_back(y0);
  g_clips.push_back(x1);
  g_clips.push_back(y1);
}

void UiPopClip(void)
{
  if (!g_clips.empty())
    g_clips.resize(g_clips.size() - 4);
}

void UiFontMetrics(UiFace face, float fSize, float *pfAscent, float *pfDescent, float *pfLineGap)
{
  Face *f = FaceOf(face);
  if (f == NULL) {
    *pfAscent = fSize * 0.8f;
    *pfDescent = -fSize * 0.2f;
    *pfLineGap = 0.0f;
    return;
  }
  int nAscent, nDescent, nGap;
  stbtt_GetFontVMetrics(&f->font, &nAscent, &nDescent, &nGap);
  const float fScale = stbtt_ScaleForMappingEmToPixels(&f->font, roundf(fSize));
  *pfAscent = nAscent * fScale;
  *pfDescent = nDescent * fScale;
  *pfLineGap = nGap * fScale;
}

float UiTextWidth(UiFace face, float fSize, float fSpacing, const char *pszUtf8, int nBytes)
{
  const int nSize = static_cast<int>(roundf(fSize));
  const char *p = pszUtf8;
  const char *pEnd = pszUtf8 + (nBytes < 0 ? strlen(pszUtf8) : static_cast<size_t>(nBytes));
  float fWidth = 0.0f;
  int nPrev = 0;
  for (unsigned int c; (c = NextChar(&p, pEnd)) != 0;) {
    const Glyph *g = GlyphOf(face, nSize, c);
    if (g == NULL)
      continue;
    fWidth += Kern(face, nSize, nPrev, g->nIndex) + g->fAdvance + fSpacing;
    nPrev = g->nIndex;
  }
  return fWidth;
}

float UiText(float x, float fBaseline, UiFace face, float fSize, float fSpacing, UiColor c, const char *pszUtf8,
             int nBytes, int nUnderline)
{
  const int nSize = static_cast<int>(roundf(fSize));
  const char *p = pszUtf8;
  const char *pEnd = pszUtf8 + (nBytes < 0 ? strlen(pszUtf8) : static_cast<size_t>(nBytes));
  const float fY = roundf(fBaseline);
  float fPen = x;
  int nPrev = 0;
  int nChar = 0;
  for (unsigned int ch; (ch = NextChar(&p, pEnd)) != 0; ++nChar) {
    const Glyph *g = GlyphOf(face, nSize, ch);
    if (g == NULL)
      continue;
    fPen += Kern(face, nSize, nPrev, g->nIndex);
    if (g->bInAtlas) {
      UiPrim *q = Push(roundf(fPen) + g->nOffsetX, fY + g->nOffsetY, static_cast<float>(g->nWidth),
                       static_cast<float>(g->nHeight), c, UI_GLYPH);
      q->uv[0] = g->u0;
      q->uv[1] = g->v0;
      q->uv[2] = g->u1;
      q->uv[3] = g->v1;
    }
    if (nChar == nUnderline) {
      // CSS's underline for the hotkeys: 2 units thick, 5 below the baseline at the nav's size
      const float fThick = fmaxf(1.0f, roundf(fSize * 0.09f));
      UiFill(roundf(fPen), fY + roundf(fSize * 0.2f), roundf(g->fAdvance), fThick, c);
    }
    fPen += g->fAdvance + fSpacing;
    nPrev = g->nIndex;
  }
  return fPen - x;
}

const char *UiFaceSource(UiFace face)
{
  FacesLoad();
  const Face &f = g_faces[FaceNumber(face)];
  return f.bLoaded ? f.szSource : "none";
}

const UiPrim *UiPrims(int *pnCount)
{
  *pnCount = static_cast<int>(g_prims.size());
  return g_prims.empty() ? NULL : g_prims.data();
}

const unsigned char *UiAtlas(int *pnWidth, int *pnHeight)
{
  if (g_atlas.empty())
    g_atlas.assign(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
  *pnWidth = kAtlasSize;
  *pnHeight = kAtlasSize;
  return g_atlas.data();
}

bool UiAtlasDirty(int *pnFirstRow, int *pnRows)
{
  if (g_nDirtyFirst < 0)
    return false;
  *pnFirstRow = g_nDirtyFirst;
  *pnRows = g_nDirtyLast - g_nDirtyFirst;
  return *pnRows > 0;
}

void UiAtlasClean(void)
{
  g_nDirtyFirst = -1;
  g_nDirtyLast = -1;
}

const unsigned char *UiPictures(int *pnWidth, int *pnHeight)
{
  if (g_pictures.empty())
    g_pictures.assign(static_cast<size_t>(kPictureAtlasW) * kPictureAtlasH * 4, 0);
  *pnWidth = kPictureAtlasW;
  *pnHeight = kPictureAtlasH;
  return g_pictures.data();
}

bool UiPicturesDirty(int *pnFirstRow, int *pnRows)
{
  if (g_nPicturesFirst < 0)
    return false;
  *pnFirstRow = g_nPicturesFirst;
  *pnRows = g_nPicturesLast - g_nPicturesFirst;
  return *pnRows > 0;
}

void UiPicturesClean(void)
{
  g_nPicturesFirst = -1;
  g_nPicturesLast = -1;
}

void UiLayoutBegin(float fPointerX, float fPointerY, bool bPointerDown)
{
  Clay_Dimensions dims;
  dims.width = static_cast<float>(g_nWidth);
  dims.height = static_cast<float>(g_nHeight);
  if (g_pClay == NULL) {
    const uint32_t nMemory = Clay_MinMemorySize();
    g_pClayMemory = SDL_malloc(nMemory);
    Clay_ErrorHandler handler;
    SDL_zero(handler);
    handler.errorHandlerFunction = ClayError;
    g_pClay = Clay_Initialize(Clay_CreateArenaWithCapacityAndMemory(nMemory, g_pClayMemory), dims, handler);
    Clay_SetMeasureTextFunction(ClayMeasure, NULL);
  }
  Clay_SetCurrentContext(g_pClay);
  Clay_SetLayoutDimensions(dims);
  Clay_Vector2 at;
  at.x = fPointerX;
  at.y = fPointerY;
  Clay_SetPointerState(at, bPointerDown);
  // Clay keeps scroll records for ten clipping elements and lets go of those not laid out last
  // frame only here, else a later one's childOffset does nothing; the scrolling is the screens'
  Clay_UpdateScrollContainers(false, Clay_Vector2{0.0f, 0.0f}, 0.0f);
  Clay_BeginLayout();
}

void UiLayoutEnd(void)
{
  ClayDraw(Clay_EndLayout());
  g_motions.clear();
}

void UiSetGlow(UiColor c)
{
  g_glow = c;
}

void UiMotionSet(unsigned int nClayId, const UiMotion &m)
{
  Motion mo;
  SDL_zero(mo);
  mo.nId = nClayId;
  mo.m = m;
  g_motions.push_back(mo);
}

float UiBezier(float t, float x1, float y1, float x2, float y2)
{
  if (t <= 0.0f)
    return 0.0f;
  if (t >= 1.0f)
    return 1.0f;
  // the curve's parameter u whose x is t, by bisection, then its y
  const auto At = [](float u, float a, float b) {
    const float v = 1.0f - u;
    return 3.0f * v * v * u * a + 3.0f * v * u * u * b + u * u * u;
  };
  float lo = 0.0f, hi = 1.0f, u = t;
  for (int i = 0; i < 24; ++i) {
    u = (lo + hi) * 0.5f;
    if (At(u, x1, x2) < t)
      lo = u;
    else
      hi = u;
  }
  return At(u, y1, y2);
}

float UiEase(float t) { return UiBezier(t, 0.15f, 0.6f, 0.4f, 1.0f); }
float UiEaseEven(float t) { return UiBezier(t, 0.3f, 0.55f, 0.7f, 0.45f); }

float UiSpan(float t, float fFrom, float fLength)
{
  const float f = fLength > 0.0f ? (t - fFrom) / fLength : (t >= fFrom ? 1.0f : 0.0f);
  return f < 0.0f ? 0.0f : f > 1.0f ? 1.0f : f;
}
