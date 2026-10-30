// port_maptext.cpp -- a map's text as StarCraft: Remastered reads it (port_maptext.h):
// UTF-8 and a Korean map's CP949 decoded, and the characters 1.17's fonts lack drawn from a
// TrueType face in 1.17's glyph form.
#include "port_maptext.h"

#include <math.h>
#include <algorithm>
#include <map>
#include <string.h>
#include <utility>
#include <vector>
#include <SDL3/SDL.h>
#include <windows.h>  // FindResourceA, the POSIX layer's on other systems
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "../../gen/cp949.inc"
#include "../data/port_language.h"  // PortLanguageGame

void *PortCascRead(const char *pszName, size_t *pnSize);  // port_casc.cpp
int PortCascAvailable(void);

namespace {

// The map last read has Korean strings in CP949.
bool g_bKorean = false;

bool Cp949Trail(unsigned char b)
{
  return (b >= 0x41 && b <= 0x5A) || (b >= 0x61 && b <= 0x7A) || (b >= 0x81 && b <= 0xFE);
}

unsigned Cp949(unsigned char bLead, unsigned char bTrail)
{
  if (bLead < 0x81 || bLead > 0xFE || bTrail < 0x41 || bTrail > 0xFE)
    return 0;
  return kCp949[bLead - 0x81][bTrail - 0x41];
}

int Utf8Lead(unsigned char b)
{
  return b >= 0xC2 && b <= 0xDF ? 1 : b >= 0xE0 && b <= 0xEF ? 2 : b >= 0xF0 && b <= 0xF4 ? 3 : 0;
}

// Whether b may stand second after the lead, which rules out the long forms, the surrogates
// and code points past U+10FFFF.
bool Utf8Second(unsigned char bLead, unsigned char b)
{
  if (bLead == 0xE0)
    return b >= 0xA0 && b <= 0xBF;
  if (bLead == 0xED)
    return b >= 0x80 && b <= 0x9F;
  if (bLead == 0xF0)
    return b >= 0x90 && b <= 0xBF;
  if (bLead == 0xF4)
    return b >= 0x80 && b <= 0x8F;
  return b >= 0x80 && b <= 0xBF;
}

// A valid UTF-8 character of two bytes or more at p, before pEnd: its byte count, else 0.
int Utf8At(const unsigned char *p, const unsigned char *pEnd)
{
  const int nMore = Utf8Lead(p[0]);
  if (nMore == 0 || pEnd - p <= nMore)
    return 0;
  if (!Utf8Second(p[0], p[1]))
    return 0;
  for (int i = 2; i <= nMore; ++i)
    if (p[i] < 0x80 || p[i] > 0xBF)
      return 0;
  return nMore + 1;
}

// Windows-1252's characters at 0x80 to 0x9F, 0 where it has none.
const unsigned short k1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
    0x2039, 0x0152, 0,      0x017D, 0,      0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178,
};

// The TrueType faces, in the order a character is looked for: Remastered's Korean face (its
// Hangul, arrows and shapes), then its global face (every script), then Hyperion's own Noto
// faces (Latin, Greek and Cyrillic, then Japanese, Korean and Chinese, cut to each language's
// common characters, tools/fetch_fonts.py), else the system's. A game in Japanese or Chinese
// looks in Remastered's face for that language first, and the menus' language puts its own
// Noto face before the other East Asian ones, so the characters the scripts share are drawn in
// that language's forms (FaceOrder).
enum FaceFrom { FACE_CASC, FACE_OWN, FACE_SYSTEM };

struct Face {
  const char *pszName;
  FaceFrom from;
  bool bTried;
  void *pData;
  stbtt_fontinfo info;
};

Face g_faces[] = {
    {"font\\Kostar.ttf", FACE_CASC, false, NULL, {}},
    {"font\\bl.ttf", FACE_CASC, false, NULL, {}},
    {"font\\UDTypos58B_P_H.ttf", FACE_CASC, false, NULL, {}},
    {"font\\arudjingxiheiu20_b.ttf", FACE_CASC, false, NULL, {}},
    {"HYPERION_FONT_NOTO", FACE_OWN, false, NULL, {}},
    {"HYPERION_FONT_NOTO_JP", FACE_OWN, false, NULL, {}},
    {"HYPERION_FONT_NOTO_KR", FACE_OWN, false, NULL, {}},
    {"HYPERION_FONT_NOTO_SC", FACE_OWN, false, NULL, {}},
    {"HYPERION_FONT_NOTO_TC", FACE_OWN, false, NULL, {}},
    {"malgun.ttf", FACE_SYSTEM, false, NULL, {}},
    {"seguisym.ttf", FACE_SYSTEM, false, NULL, {}},
    {"msyh.ttc", FACE_SYSTEM, false, NULL, {}},
    {"msgothic.ttc", FACE_SYSTEM, false, NULL, {}},
};

stbtt_fontinfo *FaceLoad(Face &f)
{
  if (!f.bTried) {
    f.bTried = true;
    size_t cb = 0;
    if (f.from == FACE_CASC) {
      if (PortCascAvailable())
        f.pData = PortCascRead(f.pszName, &cb);
    } else if (f.from == FACE_OWN) {
      // the program's own resource, mapped for the process's life
      HRSRC hRes = FindResourceA(NULL, f.pszName, MAKEINTRESOURCEA(10));  // RT_RCDATA
      HGLOBAL hData = hRes != NULL ? LoadResource(NULL, hRes) : NULL;
      const unsigned char *p = hData != NULL ? static_cast<const unsigned char *>(LockResource(hData)) : NULL;
      if (p != NULL && stbtt_InitFont(&f.info, p, stbtt_GetFontOffsetForIndex(p, 0)) != 0)
        f.pData = const_cast<unsigned char *>(p);
      return f.pData != NULL ? &f.info : NULL;
    } else {
#if defined(_WIN32)
      char szPath[MAX_PATH + 64];
      const UINT n = GetWindowsDirectoryA(szPath, MAX_PATH);
      if (n != 0 && n < MAX_PATH) {
        SDL_snprintf(szPath + n, sizeof szPath - n, "\\Fonts\\%s", f.pszName);
        f.pData = SDL_LoadFile(szPath, &cb);
      }
#endif
    }
    const unsigned char *p = static_cast<const unsigned char *>(f.pData);
    if (p != NULL && stbtt_InitFont(&f.info, p, stbtt_GetFontOffsetForIndex(p, 0)) == 0) {
      SDL_free(f.pData);
      f.pData = NULL;
    }
  }
  return f.pData != NULL ? &f.info : NULL;
}

const std::vector<Face *> &FaceOrder()
{
  static std::vector<Face *> s_order;
  if (s_order.empty()) {
    const char *pszGame = PortLanguageGame();
    const char *pszFirst = SDL_strcmp(pszGame, "jaJP") == 0 ? "font\\UDTypos58B_P_H.ttf"
                           : SDL_strncmp(pszGame, "zh", 2) == 0 ? "font\\arudjingxiheiu20_b.ttf"
                                                                 : NULL;
    const char *pszMenus = PortLanguageChosen();
    const char *pszNoto = SDL_strcmp(pszMenus, "jaJP") == 0   ? "HYPERION_FONT_NOTO_JP"
                          : SDL_strcmp(pszMenus, "koKR") == 0 ? "HYPERION_FONT_NOTO_KR"
                          : SDL_strcmp(pszMenus, "zhCN") == 0 ? "HYPERION_FONT_NOTO_SC"
                          : SDL_strcmp(pszMenus, "zhTW") == 0 ? "HYPERION_FONT_NOTO_TC"
                                                              : NULL;
    // the language's Remastered face, Remastered's others, Noto's Latin, the language's Noto
    // face, Noto's others, the system's
    const auto rank = [&](const Face &f) {
      const bool bFirst = pszFirst != NULL && SDL_strcmp(f.pszName, pszFirst) == 0;
      const bool bNoto = pszNoto != NULL && SDL_strcmp(f.pszName, pszNoto) == 0;
      return f.from == FACE_CASC     ? (bFirst ? 0 : 1)
             : f.from == FACE_SYSTEM ? 5
             : SDL_strcmp(f.pszName, "HYPERION_FONT_NOTO") == 0 ? 2
             : bNoto ? 3 : 4;
    };
    for (Face &f : g_faces)
      s_order.push_back(&f);
    std::stable_sort(s_order.begin(), s_order.end(), [&](const Face *a, const Face *b) { return rank(*a) < rank(*b); });
  }
  return s_order;
}

stbtt_fontinfo *FaceFor(unsigned cp, int *pnGlyph)
{
  for (Face *pFace : FaceOrder()) {
    Face &f = *pFace;
    stbtt_fontinfo *pInfo = FaceLoad(f);
    if (pInfo == NULL)
      continue;
    const int nGlyph = stbtt_FindGlyphIndex(pInfo, static_cast<int>(cp));
    if (nGlyph != 0) {
      *pnGlyph = nGlyph;
      return pInfo;
    }
  }
  return NULL;
}

// The glyphs made, by the font's height and baseline and the code point, which are all a
// glyph is made from; a glyph's bytes never move once made, since draw_char's GPU path knows a
// glyph by its address (PortSceneGlyph).
std::map<std::pair<int, unsigned>, std::vector<unsigned char>> g_glyphs;

// One glyph's pixels, each a colour of the font's eight (0 for none), as draw_char's bits: a
// byte a pixel drawn, its colour in the low 3 bits and the pixels passed over before it in the
// top 5, with bytes of colour 0 to pass over more than 31 and to the glyph's end.
void Encode(const std::vector<unsigned char> &pixels, std::vector<unsigned char> *pOut)
{
  int nSkip = 0;
  for (const unsigned char c : pixels) {
    if (c == 0) {
      ++nSkip;
      continue;
    }
    while (nSkip > 31) {
      pOut->push_back(static_cast<unsigned char>(31 << 3));
      nSkip -= 31;
    }
    pOut->push_back(static_cast<unsigned char>(nSkip << 3 | c));
    nSkip = 0;
  }
  while (nSkip > 0) {
    const int n = nSkip > 31 ? 31 : nSkip;
    pOut->push_back(static_cast<unsigned char>(n << 3));
    nSkip -= n;
  }
}

}  // namespace

void MapTextStrings(const void *pStrings, size_t cb, bool bWide)
{
  // each string of the table, to its NUL: Korean when it is UTF-8 with Hangul in it, or not
  // UTF-8 and CP949 with Hangul in it; a map's program kept in its strings is binary, which
  // seldom makes either whole
  const unsigned char *p = static_cast<const unsigned char *>(pStrings);
  const size_t nSlot = bWide ? 4 : 2;
  g_bKorean = false;
  if (p == NULL || cb < nSlot)
    return;
  const size_t nCount = bWide ? (p[0] | p[1] << 8 | p[2] << 16 | static_cast<size_t>(p[3]) << 24) : (p[0] | p[1] << 8);
  for (size_t i = 0; i < nCount && nSlot * (i + 2) <= cb; ++i) {
    const unsigned char *q = p + nSlot * (i + 1);
    const size_t nOff = bWide ? (q[0] | q[1] << 8 | q[2] << 16 | static_cast<size_t>(q[3]) << 24) : (q[0] | q[1] << 8);
    if (nOff >= cb)
      continue;
    const unsigned char *const pStart = p + nOff;
    const unsigned char *pEnd = static_cast<const unsigned char *>(memchr(pStart, 0, cb - nOff));
    if (pEnd == NULL || pEnd - pStart > 4096)
      continue;
    bool bUtf8 = true, bCp949 = true, bHigh = false, bHangul8 = false, bHangul949 = false;
    for (const unsigned char *r = pStart; r < pEnd;) {
      if (*r < 0x80) {
        ++r;
        continue;
      }
      bHigh = true;
      const int n = Utf8At(r, pEnd);
      if (n == 0) {
        bUtf8 = false;
      } else {
        unsigned cp = r[0] & (0x3F >> (n - 1));
        for (int k = 1; k < n; ++k)
          cp = cp << 6 | (r[k] & 0x3F);
        bHangul8 = bHangul8 || (cp >= 0xAC00 && cp <= 0xD7A3);
      }
      const unsigned cp949 = r + 1 < pEnd && Cp949Trail(r[1]) ? Cp949(r[0], r[1]) : 0;
      if (cp949 == 0)
        bCp949 = false;
      bHangul949 = bHangul949 || (cp949 >= 0xAC00 && cp949 <= 0xD7A3);
      r += n != 0 ? n : cp949 != 0 ? 2 : 1;
    }
    if (bHigh && ((bUtf8 && bHangul8) || (!bUtf8 && bCp949 && bHangul949))) {
      g_bKorean = true;
      return;
    }
  }
}

int MapTextChar(const char *p, const char *pEnd, unsigned *pcp)
{
  const unsigned char *pb = reinterpret_cast<const unsigned char *>(p);
  if (pb[0] < 0x80)
    return 0;
  // a NUL ends the string, which no lead or trail byte is
  const unsigned char *pLimit = pEnd != NULL ? reinterpret_cast<const unsigned char *>(pEnd) : pb + 4;
  for (const unsigned char *r = pb + 1; r < pLimit && r < pb + 4; ++r)
    if (*r == 0) {
      pLimit = r;
      break;
    }
  const int n = Utf8At(pb, pLimit);
  if (n != 0) {
    unsigned cp = pb[0] & (0x3F >> (n - 1));
    for (int k = 1; k < n; ++k)
      cp = cp << 6 | (pb[k] & 0x3F);
    *pcp = cp;
    return n;
  }
  if (g_bKorean && pb + 1 < pLimit && Cp949Trail(pb[1])) {
    const unsigned cp = Cp949(pb[0], pb[1]);
    if (cp != 0) {
      *pcp = cp;
      return 2;
    }
  }
  return 0;
}

const char *MapTextCharStart(const char *pStart, const char *p)
{
  const char *q = pStart;
  while (q < p) {
    unsigned cp;
    const int n = MapTextChar(q, NULL, &cp);
    if (n == 0) {
      ++q;
      continue;
    }
    if (q + n > p)
      return q;
    q += n;
  }
  return p;
}

int MapTextByte1252(unsigned cp)
{
  if (cp >= 0xA0 && cp <= 0xFF)
    return static_cast<int>(cp);
  for (int i = 0; i < 32; ++i)
    if (k1252High[i] == cp && cp != 0)
      return 0x80 + i;
  return -1;
}

std::string MapTextUtf8(const char *psz)
{
  std::string s;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(psz);
  auto put = [&s](unsigned cp) {
    if (cp < 0x80) {
      s += static_cast<char>(cp);
    } else if (cp < 0x800) {
      s += static_cast<char>(0xC0 | cp >> 6);
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += static_cast<char>(0xE0 | cp >> 12);
      s += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      s += static_cast<char>(0xF0 | cp >> 18);
      s += static_cast<char>(0x80 | (cp >> 12 & 0x3F));
      s += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    }
  };
  while (p != NULL && *p != 0) {
    if (*p < 0x20) {
      ++p;
    } else if (*p < 0x80) {
      s += static_cast<char>(*p++);
    } else {
      const unsigned char *pEnd = p + 1;
      while (pEnd < p + 4 && *pEnd != 0)
        ++pEnd;
      const int n = Utf8At(p, pEnd);
      const unsigned cp949 = Cp949Trail(p[1]) ? Cp949(p[0], p[1]) : 0;
      if (n != 0) {
        s.append(reinterpret_cast<const char *>(p), n);
        p += n;
      } else if (cp949 >= 0xAC00 && cp949 <= 0xD7A3) {
        put(cp949);
        p += 2;
      } else {
        put(*p >= 0xA0 || k1252High[*p - 0x80] == 0 ? *p : k1252High[*p - 0x80]);
        ++p;
      }
    }
  }
  return s;
}

const stbtt_fontinfo *MapTextFace(unsigned cp, int *pnGlyph)
{
  return FaceFor(cp, pnGlyph);
}

void MapTextGlyphBox(const stbtt_fontinfo *pFont, int nGlyph, float fScaleX, float fScaleY, float fShiftX,
                     int *px0, int *py0, int *px1, int *py1)
{
  stbtt_vertex *pv = NULL;
  const int n = stbtt_GetGlyphShape(pFont, nGlyph, &pv);
  if (n <= 0) {
    *px0 = *py0 = *px1 = *py1 = 0;
    return;
  }
  // every point and control point: a curve stays inside its control points
  int xMin = pv[0].x, xMax = pv[0].x, yMin = pv[0].y, yMax = pv[0].y;
  for (int i = 0; i < n; ++i) {
    const int axs[] = {pv[i].x, pv[i].cx, pv[i].cx1}, ays[] = {pv[i].y, pv[i].cy, pv[i].cy1};
    const int nPoints = pv[i].type == STBTT_vcubic ? 3 : pv[i].type == STBTT_vcurve ? 2 : 1;
    for (int k = 0; k < nPoints; ++k) {
      xMin = SDL_min(xMin, axs[k]);
      xMax = SDL_max(xMax, axs[k]);
      yMin = SDL_min(yMin, ays[k]);
      yMax = SDL_max(yMax, ays[k]);
    }
  }
  stbtt_FreeShape(pFont, pv);
  *px0 = static_cast<int>(floorf(xMin * fScaleX + fShiftX));
  *py0 = static_cast<int>(floorf(-yMax * fScaleY));
  *px1 = static_cast<int>(ceilf(xMax * fScaleX + fShiftX));
  *py1 = static_cast<int>(ceilf(-yMin * fScaleY));
}

void MapTextGlyphDraw(const stbtt_fontinfo *pFont, int nGlyph, unsigned char *pOut, int nW, int nH, int nStride,
                      float fScaleX, float fScaleY, float fShiftX, int x0, int y0)
{
  stbtt_vertex *pv = NULL;
  const int n = stbtt_GetGlyphShape(pFont, nGlyph, &pv);
  if (n > 0 && nW > 0 && nH > 0) {
    stbtt__bitmap gbm;
    gbm.pixels = pOut;
    gbm.w = nW;
    gbm.h = nH;
    gbm.stride = nStride;
    stbtt_Rasterize(&gbm, 0.35f, pv, n, fScaleX, fScaleY, fShiftX, 0.0f, x0, y0, 1, pFont->userdata);
  }
  stbtt_FreeShape(pFont, pv);
}

static int g_nSqueeze = 100;

void MapTextSqueeze(int nPercent)
{
  g_nSqueeze = nPercent < 50 ? 50 : nPercent > 100 ? 100 : nPercent;
}

const void *MapTextGlyph(int nMaxHeight, int nBaseline, unsigned cp)
{
  // the squeeze above a code point's 21 bits
  const std::pair<int, unsigned> key((nMaxHeight & 0xFFFF) << 16 | (nBaseline & 0xFFFF),
                                     cp | static_cast<unsigned>(100 - g_nSqueeze) << 24);
  auto it = g_glyphs.find(key);
  if (it != g_glyphs.end())
    return it->second.empty() ? NULL : it->second.data();
  std::vector<unsigned char> &out = g_glyphs[key];
  int nGlyph = 0;
  stbtt_fontinfo *pInfo = FaceFor(cp, &nGlyph);
  if (pInfo == NULL || nMaxHeight <= 0)
    return NULL;
  // the face's height from its highest to its lowest point as the font's line, less a pixel
  const float fScale = stbtt_ScaleForPixelHeight(pInfo, static_cast<float>(nMaxHeight > 9 ? nMaxHeight - 1 : nMaxHeight));
  const float fScaleX = fScale * static_cast<float>(g_nSqueeze) / 100.0f;
  int x0, y0, x1, y1, nAdvance, nBearing;
  MapTextGlyphBox(pInfo, nGlyph, fScaleX, fScale, 0.0f, &x0, &y0, &x1, &y1);
  stbtt_GetGlyphHMetrics(pInfo, nGlyph, &nAdvance, &nBearing);
  const int nInkW = x1 > x0 ? x1 - x0 : 0, nInkH = y1 > y0 ? y1 - y0 : 0;
  const int nXOff = x0 > 0 ? x0 : 0;
  int nYOff = nBaseline + y0;
  if (nYOff < 0)
    nYOff = 0;
  // the ink and its shadow, a pixel right and down, as wide as the face's advance asks
  const int nAdvancePx = static_cast<int>(nAdvance * fScaleX + 0.5f);
  int nW = nInkW + 1;
  if (nW < nAdvancePx - nXOff)
    nW = nAdvancePx - nXOff;
  int nH = nInkH + 1;
  if (nW > 255)
    nW = 255;
  if (nH > 255)
    nH = 255;
  std::vector<unsigned char> ink(static_cast<size_t>(nInkW) * nInkH);
  if (!ink.empty())
    MapTextGlyphDraw(pInfo, nGlyph, ink.data(), nInkW, nInkH, nInkW, fScaleX, fScale, 0.0f, x0, y0);
  // coverage as the font's colours: 1 the letter, 2 and 3 its softer edges, 7 its shadow
  std::vector<unsigned char> pixels(static_cast<size_t>(nW) * nH, 0);
  auto inkAt = [&](int x, int y) -> int {
    if (x < 0 || y < 0 || x >= nInkW || y >= nInkH)
      return 0;
    const int c = ink[static_cast<size_t>(y) * nInkW + x];
    return c >= 150 ? 1 : c >= 90 ? 2 : c >= 50 ? 3 : 0;
  };
  for (int y = 0; y < nH; ++y)
    for (int x = 0; x < nW; ++x) {
      int c = inkAt(x, y);
      if (c == 0 && inkAt(x - 1, y - 1) != 0)
        c = 7;
      pixels[static_cast<size_t>(y) * nW + x] = static_cast<unsigned char>(c);
    }
  out.push_back(static_cast<unsigned char>(nW));
  out.push_back(static_cast<unsigned char>(nH));
  out.push_back(static_cast<unsigned char>(nXOff > 255 ? 255 : nXOff));
  out.push_back(static_cast<unsigned char>(nYOff > 255 ? 255 : nYOff));
  Encode(pixels, &out);
  return out.data();
}
