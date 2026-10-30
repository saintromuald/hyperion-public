// port_front_home.cpp -- the front end's home screen (docs/front-end.md): the nav bar, the
// profile card, the taskbar, the Menu, the hero, the world's plate and the news, and the
// custom draws they use. port_front.cpp's Home lays them out. Presentation only.
#include <windows.h>
#include <math.h>
#include <string.h>
#include <string>
#include <vector>
#include <SDL3/SDL.h>
#include "port_frontkit.h"
#include "port_frontscreens.h"
#include "port_front.h"
#include "../data/port_mods.h"
#include "port_ui.h"
#include "port_world.h"
#include "port_front_internal.h"

using namespace FrontKit;

namespace Front {

// ---------------------------------------------------------------------------
// Drawn pieces the layout leaves room for.

// SC2's bell, after the design's SVG: the body, its rim, the clapper.
void DrawBell(float x, float y, float w, float h, void *pCtx)
{
  const UiColor c = *static_cast<const UiColor *>(pCtx);
  const float s = UiPx(1.0f);
  const float cx = x + w * 0.5f;
  const float cy = y + h * 0.5f;
  const float t = fmaxf(1.0f, roundf(1.6f * s));
  UiRing(cx - 6.5f * s, cy - 8.0f * s, 13.0f * s, 14.0f * s, c, 6.5f * s, t);
  UiFill(cx - 6.5f * s, cy - 1.0f * s, t, 5.0f * s, c);
  UiFill(cx + 6.5f * s - t, cy - 1.0f * s, t, 5.0f * s, c);
  UiFill(cx - 8.5f * s, cy + 4.0f * s, 17.0f * s, t, c, t * 0.5f);
  UiFill(cx - 2.0f * s, cy + 6.5f * s, 4.0f * s, 3.0f * s, c, 2.0f * s);
}

// ---------------------------------------------------------------------------
// The home screen.

// How lit a thing under the mouse is, eased the design's 0.4 s each way (--sc-t-hover).
float Ease(float *pf, bool bOn)
{
  const float fStep = g_fFrameDt / 0.4f;
  *pf = bOn ? fminf(1.0f, *pf + fStep) : fmaxf(0.0f, *pf - fStep);
  return UiEaseEven(*pf);
}

float Seconds(void)
{
  return SDL_GetTicks() / 1000.0f;
}

// Across a rect, a colour that fades in from nothing and out again (a CSS linear-gradient
// transparent, c, transparent), in one piece: drawn as two halves, the column where they met was
// thinner, a line down the current section's glow (the user, 2026-10-26: "a line down 1 pixel
// worth where it seems like the tone is off").
void Fade3(float x, float y, float w, float h, UiColor c)
{
  UiColor t = c;
  t.a = 0.0f;
  UiGradientMid(x, y, w, h, c, t);
}

// The nav bar's ground (front.css .d-nav): the planet blurred under black, a black top row,
// the bar's gradient to its foot, the lit line along it, and the two highlight sweeps that
// cross it every 8 s.
void DrawNavBar(float x, float y, float w, float h, void *)
{
  const float u = Sc(1.0f);
  UiBlur(x, y, w, h, 0.0f, Sc(7));
  UiFill(x, y, w, h, kScBarUnder);
  UiFill(x, y, w, u, kScBlack);
  UiGradient(x, y + u, w, 63 * u, kScBarTop, kScBarBot, false);
  UiGradient(x, y + 64 * u, w, 4 * u, kScBarBot, kScBarEdge, false);
  UiGradient(x, y + 68 * u, w, h - 68 * u, kScBarEdge, kScBarBot, false);
  const float t = fmodf(Seconds(), 8.0f) / 8.0f;
  UiColor s = kScSweep;
  s.a *= t < 0.125f ? t / 0.125f : t > 0.875f ? (1.0f - t) / 0.125f : 1.0f;
  Fade3(x - 386 * u + t * (w + 386 * u), y, 386 * u, 2 * u, s);
  const float t2 = fmodf(Seconds() + 6.0f, 8.0f) / 8.0f;  // the foot's starts 2 s later
  UiColor s2 = kScSweep;
  s2.a *= t2 < 0.25f ? 0.0f : t2 < 0.56f ? (t2 - 0.25f) / 0.31f : t2 > 0.75f ? (1.0f - t2) / 0.25f : 1.0f;
  Fade3(x - 396 * u + t2 * (w + 396 * u), y + 68 * u, 396 * u, 2 * u, s2);
}

// A divider: 2 units, 4 in from the top and the foot.
void DrawDivider(float x, float y, float w, float h, void *)
{
  UiGradient(x, y + Sc(4), w, h - Sc(8), kScDividerTop, kScDividerBot, false);
}

// The home mark: SC in StarCraft's lettering, lit along its edges in the bar's ink, as SC2
// draws its home icon (the user's pick, 2026-10-19; the design center's brand/sc-mark.md). The
// letters are four polygons in cap heights, y up, centred on 0, 1.93 wide and 1 high, which the
// design session measured from the logo and built itself (brand/sc-mark.json): nothing of the
// game's files. Each polygon is listed as it is there.
struct MarkPoint {
  float x, y;
};

// S, top blade, 7 points
const MarkPoint kMark0[] = {
    {-0.84f, 0.5f}, {0.01577f, 0.5f}, {-0.19577f, 0.30769f}, {-0.25346f, 0.36538f},
    {-0.84f, 0.36538f}, {-0.86885f, 0.39423f}, {-0.86885f, 0.47115f},
};

// S, spine, bowl and bottom blade, 39 points
const MarkPoint kMark1[] = {
    {-0.81115f, 0.30769f}, {-0.48423f, 0.30769f}, {-0.08038f, -0.15385f}, {-0.06985f, -0.16668f},
    {-0.05992f, -0.17992f}, {-0.05061f, -0.1935f}, {-0.04196f, -0.20737f}, {-0.034f, -0.22148f},
    {-0.02676f, -0.23578f}, {-0.02028f, -0.25021f}, {-0.01458f, -0.26472f}, {-0.0097f, -0.27926f},
    {-0.00566f, -0.29378f}, {-0.00251f, -0.30822f}, {-0.00027f, -0.32253f}, {0.00103f, -0.33665f},
    {0.00135f, -0.35054f}, {0.00067f, -0.36414f}, {-0.00106f, -0.3774f}, {-0.00385f, -0.39027f},
    {-0.00775f, -0.40269f}, {-0.01278f, -0.4146f}, {-0.01897f, -0.42597f}, {-0.02637f, -0.43673f},
    {-0.03499f, -0.44683f}, {-0.04487f, -0.45622f}, {-0.05605f, -0.46484f}, {-0.06854f, -0.47265f},
    {-0.0824f, -0.47959f}, {-0.09764f, -0.48561f}, {-0.1143f, -0.49065f}, {-0.13241f, -0.49466f},
    {-0.15201f, -0.49759f}, {-0.17312f, -0.49939f}, {-0.19577f, -0.5f}, {-0.965f, -0.5f},
    {-0.73423f, -0.28846f}, {-0.67654f, -0.34615f}, {-0.15731f, -0.34615f},
};

// C, top blade, 5 points
const MarkPoint kMark2[] = {
    {0.10654f, 0.35692f}, {0.24962f, 0.5f}, {0.965f, 0.5f}, {0.76827f, 0.31221f},
    {0.71462f, 0.35692f},
};

// C, crescent, 99 points
const MarkPoint kMark3[] = {
    {0.07077f, 0.30327f}, {0.37481f, 0.32115f}, {0.34798f, 0.17808f}, {0.34711f, 0.15372f},
    {0.34672f, 0.12983f}, {0.34682f, 0.1064f}, {0.3474f, 0.08345f}, {0.34846f, 0.06096f},
    {0.35001f, 0.03895f}, {0.35203f, 0.0174f}, {0.35452f, -0.00367f}, {0.35749f, -0.02426f},
    {0.36093f, -0.04439f}, {0.36484f, -0.06404f}, {0.36922f, -0.08321f}, {0.37406f, -0.1019f},
    {0.37937f, -0.12012f}, {0.38514f, -0.13785f}, {0.39137f, -0.15511f}, {0.39805f, -0.17188f},
    {0.4052f, -0.18817f}, {0.41279f, -0.20398f}, {0.42084f, -0.21931f}, {0.42934f, -0.23415f},
    {0.43829f, -0.2485f}, {0.44769f, -0.26237f}, {0.45752f, -0.27575f}, {0.46781f, -0.28863f},
    {0.47853f, -0.30103f}, {0.48969f, -0.31294f}, {0.50128f, -0.32436f}, {0.51331f, -0.33528f},
    {0.52578f, -0.34571f}, {0.53867f, -0.35565f}, {0.552f, -0.36509f}, {0.56575f, -0.37403f},
    {0.57992f, -0.38247f}, {0.59452f, -0.39042f}, {0.60954f, -0.39786f}, {0.62498f, -0.40481f},
    {0.64084f, -0.41125f}, {0.65711f, -0.41719f}, {0.6738f, -0.42263f}, {0.69089f, -0.42756f},
    {0.7084f, -0.43199f}, {0.72631f, -0.43591f}, {0.74463f, -0.43933f}, {0.76335f, -0.44223f},
    {0.78248f, -0.44463f}, {0.802f, -0.44651f}, {0.82192f, -0.44788f}, {0.79305f, -0.45415f},
    {0.76456f, -0.45954f}, {0.73646f, -0.46407f}, {0.70876f, -0.46774f}, {0.68147f, -0.47056f},
    {0.6546f, -0.47255f}, {0.62817f, -0.47371f}, {0.60217f, -0.47405f}, {0.57663f, -0.47359f},
    {0.55155f, -0.47232f}, {0.52693f, -0.47027f}, {0.50279f, -0.46745f}, {0.47915f, -0.46385f},
    {0.456f, -0.45949f}, {0.43336f, -0.45439f}, {0.41124f, -0.44855f}, {0.38965f, -0.44197f},
    {0.36859f, -0.43468f}, {0.34808f, -0.42668f}, {0.32813f, -0.41797f}, {0.30874f, -0.40858f},
    {0.28994f, -0.3985f}, {0.27171f, -0.38776f}, {0.25409f, -0.37635f}, {0.23707f, -0.36428f},
    {0.22066f, -0.35158f}, {0.20488f, -0.33825f}, {0.18973f, -0.32429f}, {0.17523f, -0.30971f},
    {0.16138f, -0.29454f}, {0.1482f, -0.27877f}, {0.13568f, -0.26241f}, {0.12386f, -0.24549f},
    {0.11272f, -0.22799f}, {0.10229f, -0.20995f}, {0.09257f, -0.19135f}, {0.08357f, -0.17222f},
    {0.0753f, -0.15257f}, {0.06777f, -0.1324f}, {0.061f, -0.11172f}, {0.05498f, -0.09055f},
    {0.04974f, -0.06888f}, {0.04528f, -0.04675f}, {0.0416f, -0.02414f}, {0.03873f, -0.00107f},
    {0.03666f, 0.02244f}, {0.03542f, 0.04639f}, {0.035f, 0.07077f},
};

struct MarkPolygon {
  const MarkPoint *p;
  int n;
};
const MarkPolygon kMarkPolygons[] = {{kMark0, 7}, {kMark1, 39}, {kMark2, 5}, {kMark3, 99}};

const float kMarkW = 1.93f, kMarkH = 1.0f;  // the letters, in cap heights
const float kMarkMargin = 0.04f;            // round them in the mask, so 1.861 wide for 1 high
const float kMarkPx = 160.0f;               // the mask's pixels a cap height, drawn scaled down
const float kMarkUnits = 54.0f;             // the mask's width in the bar, in units
const float kMarkEdgeIn = 0.06f;            // the lit band: full to 0.054 in, none from 0.06
const float kMarkFill = 0.28f;              // the faint fill between the lit edges
const float kGlowPx = 48.0f;                // the glows' pixels a cap height
const float kGlowPad = 0.5f;                // the glows round the mask, in cap heights
const int kMarkSlot = 32, kGlowSlot = 33;   // port_ui.h's picture slots
int g_nGlowW, g_nGlowH;                     // a glow's pixels in slot 33, set by MarkBake

// The signed distance from a point to a polygon, negative inside: the least distance to its
// edges, its sign flipped at each edge a ray from the point crosses (Inigo Quilez's sdPolygon).
float MarkPolygonDistance(const MarkPolygon &poly, float px, float py)
{
  const MarkPoint *v = poly.p;
  float d = (px - v[0].x) * (px - v[0].x) + (py - v[0].y) * (py - v[0].y);
  float s = 1.0f;
  for (int i = 0, j = poly.n - 1; i < poly.n; j = i, ++i) {
    const float ex = v[j].x - v[i].x, ey = v[j].y - v[i].y;
    const float wx = px - v[i].x, wy = py - v[i].y;
    const float t = fminf(1.0f, fmaxf(0.0f, (wx * ex + wy * ey) / (ex * ex + ey * ey)));
    const float bx = wx - ex * t, by = wy - ey * t;
    d = fminf(d, bx * bx + by * by);
    const bool c0 = py >= v[i].y, c1 = py < v[j].y, c2 = ex * wy > ey * wx;
    if ((c0 && c1 && c2) || (!c0 && !c1 && !c2))
      s = -s;
  }
  return s * sqrtf(d);
}

// The letters' alpha at a point in cap heights, its outline antialiased over fPx pixels a cap
// height: the lit band along the outline over the faint fill (sc-mark.md, "Drawing it").
float MarkAlpha(float x, float y, float fPx)
{
  float d = 1e9f;
  for (const MarkPolygon &poly : kMarkPolygons)
    d = fminf(d, MarkPolygonDistance(poly, x, y));
  const float fCover = fminf(1.0f, fmaxf(0.0f, 0.5f - d * fPx));
  const float fEdge = fminf(1.0f, fmaxf(0.0f, (kMarkEdgeIn + d) * kMarkPx));
  return fCover * fmaxf(fEdge, kMarkFill);
}

// A gaussian blur of a w by h field, its deviation in pixels, across then down.
void MarkBlur(std::vector<float> &a, int w, int h, float fSigma)
{
  const int r = static_cast<int>(ceilf(fSigma * 3.0f));
  std::vector<float> k(static_cast<size_t>(2 * r + 1));
  float fSum = 0.0f;
  for (int i = -r; i <= r; ++i)
    fSum += k[static_cast<size_t>(i + r)] = expf(-0.5f * i * i / (fSigma * fSigma));
  for (float &f : k)
    f /= fSum;
  std::vector<float> t(a.size());
  for (int pass = 0; pass < 2; ++pass) {
    const int nLen = pass == 0 ? w : h;
    for (int y = 0; y < (pass == 0 ? h : w); ++y)
      for (int x = 0; x < nLen; ++x) {
        float f = 0.0f;
        for (int i = -r; i <= r; ++i) {
          const int at = x + i;
          if (at >= 0 && at < nLen)
            f += k[static_cast<size_t>(i + r)] * (pass == 0 ? a[static_cast<size_t>(y) * w + at]
                                                             : a[static_cast<size_t>(at) * w + y]);
        }
        t[pass == 0 ? static_cast<size_t>(y) * w + x : static_cast<size_t>(x) * w + y] = f;
      }
    a.swap(t);
  }
}

// White pixels with the field as their alpha, row j0 on, into an RGBA sheet w wide.
void MarkStore(std::vector<unsigned char> &px, const std::vector<float> &a, int w, int h, int j0)
{
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i) {
      unsigned char *p = &px[(static_cast<size_t>(j0 + j) * w + i) * 4];
      p[0] = p[1] = p[2] = 255;
      p[3] = static_cast<unsigned char>(fminf(1.0f, fmaxf(0.0f, a[static_cast<size_t>(j) * w + i])) * 255.0f + 0.5f);
    }
}

// The mask in slot 32, and in slot 33 its two glows one over the other: the design's drop
// shadows of 4 and 8 units (front.css .d-home .mk-line), the second a shadow of the mark and
// the first together, as CSS chains them. Made once.
void MarkBake(void)
{
  static bool bBaked;
  if (bBaked)
    return;
  bBaked = true;
  const float fW = kMarkW + 2 * kMarkMargin, fH = kMarkH + 2 * kMarkMargin;
  const int w = static_cast<int>(fW * kMarkPx + 0.5f), h = static_cast<int>(fH * kMarkPx + 0.5f);
  std::vector<float> a(static_cast<size_t>(w) * h);
  for (int j = 0; j < h; ++j)
    for (int i = 0; i < w; ++i)
      a[static_cast<size_t>(j) * w + i] =
          MarkAlpha(-fW * 0.5f + (i + 0.5f) / kMarkPx, fH * 0.5f - (j + 0.5f) / kMarkPx, kMarkPx);
  std::vector<unsigned char> px(static_cast<size_t>(w) * h * 4);
  MarkStore(px, a, w, h, 0);
  UiPictureSet(kMarkSlot, px.data(), w, h);

  // a unit is 1/(54/2.01) of a cap height, so the deviations of 2 and 4 units (CSS's blur
  // radius is twice the deviation) are these many glow pixels
  const float fUnit = fW / kMarkUnits * kGlowPx;
  const float gW = fW + 2 * kGlowPad, gH = fH + 2 * kGlowPad;
  const int gw = static_cast<int>(gW * kGlowPx + 0.5f), gh = static_cast<int>(gH * kGlowPx + 0.5f);
  std::vector<float> g(static_cast<size_t>(gw) * gh);
  for (int j = 0; j < gh; ++j)
    for (int i = 0; i < gw; ++i)
      g[static_cast<size_t>(j) * gw + i] =
          MarkAlpha(-gW * 0.5f + (i + 0.5f) / kGlowPx, gH * 0.5f - (j + 0.5f) / kGlowPx, kGlowPx);
  std::vector<float> g1 = g;
  MarkBlur(g1, gw, gh, 2.0f * fUnit);
  std::vector<float> g2(g.size());
  for (size_t i = 0; i < g.size(); ++i)
    g2[i] = g[i] + g1[i] * (1.0f - g[i]);
  MarkBlur(g2, gw, gh, 4.0f * fUnit);
  std::vector<unsigned char> gpx(static_cast<size_t>(gw) * gh * 2 * 4);
  MarkStore(gpx, g1, gw, gh, 0);
  MarkStore(gpx, g2, gw, gh, gh);
  UiPictureSet(kGlowSlot, gpx.data(), gw, 2 * gh);
  g_nGlowW = gw;
  g_nGlowH = gh;
}

struct MarkLook {
  float fAlpha, fHover;
};

// The mark centred in the home button, 54 units wide, at fAlpha; its first glow always, the
// second under the mouse.
void DrawMark(float x, float y, float w, float h, void *pCtx)
{
  const MarkLook &m = *static_cast<const MarkLook *>(pCtx);
  const float fCap = Sc(kMarkUnits) / (kMarkW + 2 * kMarkMargin);  // a cap height on the screen
  const float cx = x + w * 0.5f, cy = y + h * 0.5f;
  const float gw = (kMarkW + 2 * kMarkMargin + 2 * kGlowPad) * fCap;
  const float gh = (kMarkH + 2 * kMarkMargin + 2 * kGlowPad) * fCap;
  UiColor c = kScGlow;
  c.a *= m.fAlpha * m.fHover;
  const float fGlowW = static_cast<float>(g_nGlowW), fGlowH = static_cast<float>(g_nGlowH);
  UiPicturePart(cx - gw * 0.5f, cy - gh * 0.5f, gw, gh, kGlowSlot, 0.0f, fGlowH, fGlowW, 2.0f * fGlowH, c);
  c = kScGlow;
  c.a *= m.fAlpha;
  UiPicturePart(cx - gw * 0.5f, cy - gh * 0.5f, gw, gh, kGlowSlot, 0.0f, 0.0f, fGlowW, fGlowH, c);
  c = kScInkNav;
  c.a *= m.fAlpha;
  const float mw = (kMarkW + 2 * kMarkMargin) * fCap, mh = (kMarkH + 2 * kMarkMargin) * fCap;
  UiPicture(cx - mw * 0.5f, cy - mh * 0.5f, mw, mh, kMarkSlot, c);
}

// A section's frame under the mouse, and on the current one its glow, its pulse and its
// underline (front.css .d-nav .nav .hv, .sg, .ul).
struct NavLook {
  float fHover, fSel;
};

void DrawNavItem(float x, float y, float w, float h, void *pCtx)
{
  const NavLook &n = *static_cast<const NavLook *>(pCtx);
  const float u = Sc(1.0f);
  const float fFrame = fmaxf(n.fHover, n.fSel);
  if (fFrame > 0.0f) {
    UiColor glow = kScHoverGlow;
    glow.a *= fFrame;
    UiRing(x - 4 * u, y - u, w + 8 * u, h + 2 * u, glow, 3 * u, 6 * u);
    UiColor line = kScHoverLine;
    line.a *= fFrame;
    UiRing(x - u, y + 2 * u, w + 2 * u, h - 4 * u, line, 0.5f, 2 * u);
  }
  if (n.fSel > 0.0f) {
    const float fPulse = 0.5f - 0.5f * cosf(Seconds() / 2.4f * 6.2831853f);
    // the glow: an ellipse half the width and the full height, centred on the foot, fading
    // out by three quarters of the way; drawn as bands
    const float gw = w + 64 * u, cx = x + w * 0.5f;
    const int kBands = 16;
    for (int i = 0; i < kBands; ++i) {
      const float dy = (i + 0.5f) / kBands;  // from the foot up, 0 to 1
      const float half = sqrtf(fmaxf(0.0f, 0.75f * 0.75f - dy * dy)) / 0.75f;
      if (half <= 0.0f)
        continue;
      UiColor g = kScSelGlow;
      g.a *= n.fSel * (0.71f + 0.59f * fPulse * 0.5f) * (1.0f - dy / 0.75f);
      const float bw = gw * half * 0.75f, bh = h / kBands;
      Fade3(cx - bw, y + h - (i + 1) * bh, 2 * bw, bh + 0.5f, g);
    }
    UiColor l = kScSelLine;
    l.a *= n.fSel;
    Fade3(x - 42 * u, y + 68 * u, w + 74 * u, 2 * u, l);
    UiColor l2 = kScSelLine2;
    l2.a *= n.fSel * fPulse;
    Fade3(x - 32 * u + (w + 64 * u) * 0.2f, y + 68 * u, (w + 64 * u) * 0.6f, 2 * u, l2);
  }
}

// The avatar (front.css .d-avatar): SC2's unranked frame, its rings, glow and glass, and the
// face behind; under the mouse, a white band.
struct AvatarLook {
  float fHover;
};

void DrawAvatar(float x, float y, float w, float h, void *pCtx)
{
  const AvatarLook &a = *static_cast<const AvatarLook *>(pCtx);
  const float u = Sc(1.0f);
  UiFill(x + 19 * u, y + 19 * u, w - 38 * u, h - 38 * u, kScBlack, 3 * u);
  UiGradient(x + 19 * u, y + 19 * u, w - 38 * u, h - 38 * u, kScAvaFaceTop, kScAvaFaceBot, false, 3 * u);
  UiFill(x + 21 * u, y + 19 * u, w - 42 * u, 2 * u, kScAvaGlass);
  UiRing(x + 9 * u, y + 9 * u, w - 18 * u, h - 18 * u, kScBlack, 9 * u, 11 * u);
  UiRing(x + 10 * u, y + 10 * u, w - 20 * u, h - 20 * u, kScAvaOuter, 8 * u, 2 * u);
  UiRing(x + 13 * u, y + 13 * u, w - 26 * u, h - 26 * u, kScAvaMid, 6 * u, u);
  UiRing(x + 15 * u, y + 15 * u, w - 30 * u, h - 30 * u, kScAvaInner, 5 * u, 2 * u);
  UiColor g = kScAvaGlow;
  UiRing(x + 17 * u, y + 17 * u, w - 34 * u, h - 34 * u, g, 4 * u, 4 * u);
  if (a.fHover > 0.0f) {
    UiColor band = kScAvaHoverBand, hi = kScAvaHover;
    band.a *= a.fHover;
    hi.a *= a.fHover;
    UiRing(x + 10 * u, y + 10 * u, w - 20 * u, h - 20 * u, band, 8 * u, 7 * u);
    UiRing(x + 17 * u, y + 17 * u, w - 34 * u, h - 34 * u, hi, 3 * u, 2 * u);
  }
}

// The player card tab (front.css .d-card-tab): blurred, lit top and foot.
void DrawCardTab(float x, float y, float w, float h, void *)
{
  const float u = Sc(1.0f);
  UiBlur(x, y, w, h, 0.0f, Sc(7));
  UiGradient(x, y, w, h, kScPartyFillTop, kScPartyFillBot, false);
  UiFill(x, y, w, u, kScPartyTop);
  UiFill(x, y + h - 2 * u, w, 2 * u, kScPartyBot);
}

// A taskbar button (front.css .d-task): the blur behind, then SC2's small button face.
struct TaskLook {
  float fHover;
  int nIcon;  // 0 the clock's plate, 1 the menu's bars, 2 the bell
};

void DrawTask(float x, float y, float w, float h, void *pCtx)
{
  const TaskLook &t = *static_cast<const TaskLook *>(pCtx);
  const float u = Sc(1.0f);
  UiBlur(x + 8 * u, y + 8 * u, w - 16 * u, h - 16 * u, 0.0f, Sc(7));
  UiFill(x + 8 * u, y + 8 * u, w - 16 * u, h - 16 * u, kScBarUnder);
  if (t.nIcon == 0) {
    UiColor f = kScClockFill;
    f.a *= 0.78f;
    UiFill(x + 6 * u, y + 6 * u, w - 12 * u, h - 12 * u, f, 5 * u);
    UiRing(x + 5 * u, y + 5 * u, w - 10 * u, h - 10 * u, kScClockRim, 5 * u, u);
    return;
  }
  const ScLook &k = t.fHover > 0.5f ? kScTaskHi : kScTask;
  const float fx = x + 4 * u, fy = y + 4 * u, fw = w - 8 * u, fh = h - 9 * u;
  UiRing(fx - 2 * u, fy - 2 * u, fw + 4 * u, fh + 4 * u, k.halo, 8 * u, 2 * u);
  UiGradient(fx, fy, fw, fh, k.top, k.bot, false, 6 * u);
  UiFill(fx + 4 * u, fy + fh - 4 * u, fw - 8 * u, 2 * u, k.lip);
  UiRing(fx, fy, fw, fh, k.rim, 6 * u, 2 * u);
  UiColor ic = kScIcon;
  if (t.fHover > 0.0f) {
    ic.r += (kScIconHi.r - ic.r) * t.fHover;
    ic.g += (kScIconHi.g - ic.g) * t.fHover;
    ic.b += (kScIconHi.b - ic.b) * t.fHover;
  }
  const float cx = x + w * 0.5f, cy = y + h * 0.5f - u;
  if (t.nIcon == 1) {
    for (int i = -1; i <= 1; ++i) {
      UiFill(cx - 13 * u, cy + i * 9 * u - 1.5f * u + 2 * u, 26 * u, 3 * u, kScBlack, 1.5f * u);
      UiFill(cx - 13 * u, cy + i * 9 * u - 1.5f * u, 26 * u, 3 * u, ic, 1.5f * u);
    }
  } else {
    UiColor c = ic;
    DrawBell(cx - 16 * u, cy - 16 * u, 32 * u, 32 * u, &c);
  }
}

// A notification (front.css .d-toast): SC2's toast, its gradient, rim, halo and shadow, and a
// band of light running round its rim.
void DrawToast(float x, float y, float w, float h, void *)
{
  const float u = Sc(1.0f);
  UiColor sh = kDShadow;
  sh.a *= 0.35f;
  UiFill(x - 4 * u, y + 4 * u, w + 8 * u, h + 8 * u, sh, 12 * u);
  UiRing(x - 5 * u, y - 5 * u, w + 10 * u, h + 10 * u, kScToastEdge, 8 * u, 4 * u);
  UiRing(x - 3 * u, y - 3 * u, w + 6 * u, h + 6 * u, {0.0f, 0.0f, 0.0f, 0.6f}, 7 * u, 3 * u);
  UiGradient(x, y, w, h, kScToastTop, kScToastBot, false, 6 * u);
  UiRing(x, y, w, h, kScToastRim, 6 * u, 2 * u);
  // BorderShimmer: in over 1 s, held to 4, out by 5, across twice
  const float t = fmodf(Seconds(), 5.0f);
  const float a = t < 1.0f ? t : t > 4.0f ? 5.0f - t : 1.0f;
  const float p = fmodf(t, 2.5f) / 2.5f;
  UiColor s = kScToastShimmer;
  s.a = 0.55f * a;
  const float bw = w * 0.3f;
  UiPushClip(x, y, x + w, y + h);
  Fade3(x + w - p * (w + bw), y, bw, 2 * u, s);
  Fade3(x + w - p * (w + bw), y + h - 2 * u, bw, 2 * u, s);
  UiPopClip();
}

// Direction D's nav bar (front.js D_.bar, front.css .d-nav): 72 units of blurred bar, the home
// mark, then the sections between dividers. The profile, the Menu and the notifications are
// elsewhere: the avatar and card at the right, the taskbar at the foot.
void NavBar(void)
{
  Decl bar;
  bar.d.id = Id("bar");
  bar.d.layout.sizing.width = Grow();
  bar.d.layout.sizing.height = Fixed(Sc(72));
  bar.d.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
  static UiCustom ground = {0.0f, DrawNavBar, NULL};
  bar.d.custom.customData = &ground;
  Open(bar);

  static UiCustom divider = {0.0f, DrawDivider, NULL};
  const auto Divider = [&](int i) {
    Decl d;
    d.d.id = Id("navdiv", i);
    d.d.layout.sizing.width = Fixed(Sc(2));
    d.d.layout.sizing.height = Grow();
    d.d.custom.customData = &divider;
    Open(d);
    Close();
  };

  // the home button: 81 units, the mark brighter under the mouse and on home
  {
    static float fHover;
    static MarkLook look;
    const Clay_ElementId id = Id("navhome");
    if (Clicked(id))
      NavPick("menu");
    const float h = Ease(&fHover, Over(id));
    const bool bHere = g_nScreen == SCREEN_HOME;
    MarkBake();
    look.fAlpha = bHere ? 1.0f : 0.8f + 0.2f * h;
    look.fHover = h;
    static UiCustom mark = {0.0f, DrawMark, &look};
    Decl b;
    b.d.id = id;
    b.d.layout.sizing.width = Fixed(Sc(81));
    b.d.layout.sizing.height = Grow();
    b.d.custom.customData = &mark;
    Open(b);
    Close();
  }
  Divider(0);

  static float fHover[8];
  static float fSel[8];
  static NavLook looks[8];
  static UiCustom items[8];
  int k = 0;
  for (const NavEntry &n : kNav) {
    if (strcmp(n.pszId, "menu") == 0)
      continue;
    const Clay_ElementId id = Id("nav", k);
    const bool bHere = strcmp(n.pszId, ScreenId()) == 0 || (strcmp(n.pszId, "mods") == 0 && g_bMods);
    const bool bOn = Enabled(n.pszId);
    if (Clicked(id) && bOn)
      NavPick(n.pszId);
    looks[k].fHover = Ease(&fHover[k], bOn && Over(id));
    fSel[k] = bHere ? fminf(1.0f, fSel[k] + g_fFrameDt / 0.5f) : fmaxf(0.0f, fSel[k] - g_fFrameDt / 0.5f);
    looks[k].fSel = UiEase(fSel[k]);
    items[k] = {0.0f, DrawNavItem, &looks[k]};
    Decl b;
    b.d.id = id;
    b.d.layout.sizing.height = Grow();
    b.d.layout.padding = Pad(Fs(28), Fs(28), 0, 0);
    b.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    b.d.custom.customData = &items[k];
    Open(b);
    const std::string label = Upper(PortText(n.pszLabel));
    const float fLit = fmaxf(looks[k].fHover, looks[k].fSel);
    UiColor c = !bOn ? kScInkOff : kScInkNav;
    if (bOn) {
      c.r += (kScInkHi.r - c.r) * fLit;
      c.g += (kScInkHi.g - c.g) * fLit;
      c.b += (kScInkHi.b - c.b) * fLit;
    }
    const int nKey = bOn && g_nScreen == SCREEN_HOME ? KeyAt(label.c_str(), n.cKey) : -1;
    if (fLit > 0.3f)
      TextGlow(Keep(label), UI_FACE_WIDE, Fs(21), 0.02f, c, nKey);
    else
      Text(Keep(label), UI_FACE_WIDE, Fs(21), 0.02f, c, nKey);
    Close();
    Divider(k + 1);
    ++k;
  }
  Close();
}

// The avatar and the player card's tab (front.js D_.bar, front.css .d-avatar, .d-card-tab):
// the avatar 113 units square, 15 in from the right and 4 down, hanging below the bar; the
// tab 355 by 50 under the bar's right end. Both open the profile (Y).
void Profile(void)
{
  const std::string name = ProfileCurrent();
  {
    static float fHover;
    const Clay_ElementId id = Id("cardtab");
    if (Clicked(id))
      CardToggle();
    const float h = Ease(&fHover, Over(id));
    Decl t = Floating("cardtab", 0, -Sc(11), Sc(72), 21, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
    t.d.id = id;
    t.d.layout.sizing.width = Fixed(Sc(355));
    t.d.layout.sizing.height = Fixed(Sc(50));
    t.d.layout.padding = Pad(Fs(15), Fs(130), 0, 0);
    t.d.layout.childGap = Gap(Fs(10));
    t.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    static UiCustom tab = {0.0f, DrawCardTab, NULL};
    t.d.custom.customData = &tab;
    Open(t);
    Text(PortText("PROFILE"), UI_FACE_TEXT, Fs(16), 0.06f, kScInkOff);
    UiColor c = kScInkLabel;
    c.r += (kScInkHi.r - c.r) * h;
    c.g += (kScInkHi.g - c.g) * h;
    c.b += (kScInkHi.b - c.b) * h;
    const char *pszName = name.empty() ? PortText("Create a profile") : Keep(name);
    if (h > 0.3f)
      TextGlow(pszName, UI_FACE_TEXT, Fs(18), 0.0f, c);
    else
      Text(pszName, UI_FACE_TEXT, Fs(18), 0.0f, c);
    Close();
  }
  {
    static float fHover;
    static AvatarLook look;
    const Clay_ElementId id = Id("avatar");
    if (Clicked(id))
      CardToggle();
    look.fHover = Ease(&fHover, Over(id));
    Decl a = Floating("avatar", 0, -Sc(15), Sc(4), 22, CLAY_ATTACH_POINT_RIGHT_TOP, CLAY_ATTACH_POINT_RIGHT_TOP);
    a.d.id = id;
    a.d.layout.sizing.width = Fixed(Sc(113));
    a.d.layout.sizing.height = Fixed(Sc(113));
    a.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    a.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    static UiCustom ava = {0.0f, DrawAvatar, &look};
    a.d.custom.customData = &ava;
    Open(a);
    char szInitial[8] = "+";
    if (!name.empty()) {
      szInitial[0] = name[0] >= 'a' && name[0] <= 'z' ? static_cast<char>(name[0] - 32) : name[0];
      szInitial[1] = 0;
    }
    TextGlow(Keep(szInitial), UI_FACE_WIDE, Fs(40), 0.0f, kScInkNav);
    Close();
  }
}

// The taskbar (front.js D_.bar, front.css .d-task): 61 units at the screen's foot, at the
// right: the Menu (F10) and the clock. The design's bell, which folded the notifications, is left
// out for now (the user, 2026-10-26: "Remove the bell icon button next to main menu button");
// N still folds them.
void Taskbar(void)
{
  struct Btn {
    const char *pszId;
    float fRight, fWidth;
    int nIcon;
  };
  static const Btn kBtns[] = {{"taskclock", 0, 110, 0}, {"taskmenu", 103, 62, 1}};
  const int nBtns = static_cast<int>(sizeof kBtns / sizeof kBtns[0]);
  static float fHover[nBtns];
  static TaskLook looks[nBtns];
  static UiCustom faces[nBtns];
  for (int i = 0; i < nBtns; ++i) {
    const Btn &b = kBtns[i];
    const Clay_ElementId id = Id(b.pszId);
    if (Clicked(id) && b.nIcon == 1)
      g_bMenuOpen = !g_bMenuOpen;
    const bool bLit = b.nIcon == 1 && g_bMenuOpen;
    looks[i].fHover = b.nIcon == 0 ? 0.0f : fmaxf(Ease(&fHover[i], Over(id)), bLit ? 1.0f : 0.0f);
    looks[i].nIcon = b.nIcon;
    faces[i] = {0.0f, DrawTask, &looks[i]};
    Decl d = Floating(b.pszId, 0, -Sc(b.fRight), 0.0f, 20, CLAY_ATTACH_POINT_RIGHT_BOTTOM, CLAY_ATTACH_POINT_RIGHT_BOTTOM);
    d.d.id = id;
    d.d.layout.sizing.width = Fixed(Sc(b.fWidth));
    d.d.layout.sizing.height = Fixed(Sc(61));
    d.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    d.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    d.d.custom.customData = &faces[i];
    Open(d);
    if (b.nIcon == 0) {
      SDL_Time now = 0;
      SDL_DateTime dt;
      char sz[8] = "--:--";
      if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &dt, true))
        SDL_snprintf(sz, sizeof sz, "%02d:%02d", dt.hour, dt.minute);
      Text(Keep(sz), UI_FACE_TEXT, Fs(16), 0.04f, Over(id) ? kScInkNav : kScInkClock);
    }
    Close();
  }
}

// SC2's Menu panel's frame (front.js D_.menu's panel, front.css .d-menu): over a blackout, a
// frame fWidth units wide and fHeight high, or as high as what it holds with fHeight 0, its title
// 45 units down, what it holds 45 in. It is left open for the panel's content, which Close ends.
// True when the blackout was clicked.
bool ScPanel(const char *pszTitle, float fWidth, float fHeight)
{
  Decl black = Floating("menublack", 0, 0.0f, 0.0f, 60, CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP);
  black.d.id = Id("menublack");
  black.d.layout.sizing.width = Fixed(static_cast<float>(UiWidth()));
  black.d.layout.sizing.height = Fixed(static_cast<float>(UiHeight()));
  black.d.backgroundColor = Col(kScBlackout);
  const bool bOut = Clicked(black.d.id);
  Open(black);
  Close();

  Decl panel = Floating("drop", 0, 0.0f, 0.0f, 61, CLAY_ATTACH_POINT_CENTER_CENTER, CLAY_ATTACH_POINT_CENTER_CENTER);
  panel.d.id = Id("drop");
  panel.d.layout.sizing.width = Fixed(Sc(fWidth));
  panel.d.layout.sizing.height = fHeight > 0.0f ? Fixed(Sc(fHeight)) : Fit();
  panel.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  panel.d.layout.padding = Pad(Fs(45), Fs(45), Fs(30), fHeight > 0.0f ? 0.0f : Fs(40));
  panel.d.layout.childGap = Gap(Fs(14));
  static UiCustom menuFrame = {0.0f, DrawScFrame, NULL};
  panel.d.custom.customData = &menuFrame;
  Open(panel);
  Decl title;
  title.d.layout.sizing.width = Grow();
  title.d.layout.sizing.height = Fixed(Sc(40));
  title.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
  Open(title);
  TextGlow(Keep(Upper(pszTitle)), UI_FACE_WIDE, Fs(24), 0.04f, kScInkHead);
  Close();
  return bOut;
}

// SC2's Menu: ScPanel 450 units wide, the entries 355 wide and 76 apart from 84 down, the step
// back under them. The home screen's Menu and a game's (port_front_gamemenu.cpp) are this one.
int ScMenu(const char *pszTitle, const MenuItem *pItems, int nItems, const char *pszBack, char cBack)
{
  int nChosen = MENU_NONE;
  if (ScPanel(pszTitle, 450, 84 + nItems * 76 + 30 + 76 + 40))
    nChosen = MENU_BACK;
  for (int i = 0; i < nItems; ++i) {
    Decl b;
    b.d.id = Id("dropitem", i);
    b.d.layout.sizing.width = Fixed(Sc(355));
    b.d.layout.sizing.height = Fixed(Sc(62));
    if (ScButton(b, pItems[i].pszLabel, pItems[i].cKey, false, pItems[i].bEnabled))
      nChosen = i;
  }
  Decl gap;
  gap.d.layout.sizing.height = Fixed(Sc(16));
  Open(gap);
  Close();
  Decl back;
  back.d.id = Id("menuback");
  back.d.layout.sizing.width = Fixed(Sc(355));
  back.d.layout.sizing.height = Fixed(Sc(62));
  if (ScButton(back, pszBack, cBack, false))
    nChosen = MENU_BACK;
  Close();
  return nChosen;
}

// The home screen's Menu: kDrop's entries. Escape, F10 or a click on the blackout shuts it.
void MenuPanel(void)
{
  const int nItems = static_cast<int>(sizeof kDrop / sizeof kDrop[0]);
  MenuItem items[sizeof kDrop / sizeof kDrop[0]];
  for (int i = 0; i < nItems; ++i)
    items[i] = MenuItem{PortText(kDrop[i].pszLabel), kDrop[i].cKey, Enabled(kDrop[i].pszId)};
  const int n = ScMenu(PortText("Menu"), items, nItems, PortText("Back"), 'B');
  if (n == MENU_BACK)
    g_bMenuOpen = false;
  else if (n >= 0)
    Choose(kDrop[n].pszId);
}

void Hero(void)
{
  const PortMod &m = ActiveMod();
  const bool bCampaign = m.mode == "campaign";
  const bool bBuiltIn = m.bBuiltIn;
  Decl hero;
  hero.d.id = Id("hero");
  hero.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  hero.d.layout.sizing.width = Fit(Sc(900));
  hero.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  hero.d.floating.offset.x = Sc(70);
  hero.d.floating.offset.y = Sc(140);
  Open(hero);
  Text(bBuiltIn ? PortText("THE GAME") : m.mode == "tool" ? PortText("A TOOL") : PortText("ACTIVE MOD"), UI_FACE_TEXT,
       Fs(18), 0.18f, kScInkLabel);
  Decl h2;
  h2.d.layout.padding = Pad(0, 0, Fs(4), Fs(10));
  Open(h2);
  TextGlow(Keep(Upper(m.name.c_str())), UI_FACE_WIDE, Fs(76), 0.02f, kScInkHi, -1, 1.02f);
  Close();
  Decl p;
  p.d.layout.sizing.width = Fit(Sc(700));
  p.d.layout.padding = Pad(0, 0, 0, Fs(28));
  Open(p);
  Text(PortText(m.line.c_str()), UI_FACE_TEXT, Fs(20), 0.0f, kScInkTip);
  Close();
  Decl btns;
  btns.d.layout.childGap = Gap(Fs(16));
  Open(btns);
  // the main button starts what the mod offers: a built-in game's campaign, or the mod's
  // own mode; the second, the episodes, is the built-in games' only
  for (int i = 0; i < (bCampaign ? 2 : 1); ++i) {
    if (m.mode.empty())
      break;
    Decl b;
    b.d.id = Id("cta", i);
    b.d.layout.sizing.width = Fit();
    b.d.layout.sizing.width.size.minMax.min = Sc(288);
    b.d.layout.sizing.height = Fixed(Sc(52));
    if (ScButton(b, i == 0 ? PortText(m.button.c_str()) : PortText("All episodes"), 0, i == 0)) {
      if (bCampaign || i == 1) {
        Choose("campaign");
      } else if (m.mode == "bench") {
        // the benchmark's set-up, a screen of its own over the planet
        g_bMenuOpen = false;
        g_nScreen = SCREEN_BENCH;
        WorldSetView("campaign", false);
        BenchOpen(false);
      } else if (m.mode == "trainer") {
        // the trainer's entry, likewise
        g_bMenuOpen = false;
        g_nScreen = SCREEN_TRAINER;
        WorldSetView("campaign", false);
        TrainerOpen(false);
      } else if (m.mode == "lab") {
        // the AI lab's screen, likewise
        g_bMenuOpen = false;
        g_nScreen = SCREEN_LAB;
        WorldSetView("campaign", false);
        LabOpen(false);
      } else {
        g_bMenuOpen = false;
        g_nChoice = PORT_FRONT_MODE;
      }
    }
  }
  Close();
  Close();
}

void Plate(void)
{
  WorldAbout about;
  WorldAboutNow(&about);
  Decl wc;
  wc.d.id = Id("plate");
  wc.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  wc.d.layout.sizing.width = Fit(Sc(700));
  wc.d.layout.padding = Pad(Fs(18), 0, 0, 0);
  wc.d.border = Border(kScSelLine, Fs(2), 0, 0, 0);
  wc.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  wc.d.floating.attachPoints.element = CLAY_ATTACH_POINT_LEFT_BOTTOM;
  wc.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_LEFT_BOTTOM;
  wc.d.floating.offset.x = Sc(70);
  wc.d.floating.offset.y = -Sc(50);
  Open(wc);
  Text(PortText("IN ORBIT OVER"), UI_FACE_TEXT, Fs(16), 0.2f, kScInkOff);
  Decl b;
  b.d.layout.padding = Pad(0, 0, Fs(2), Fs(4));
  Open(b);
  TextGlow(Keep(Upper(about.pszName)), UI_FACE_WIDE, Fs(44), 0.12f, kScInkHi);
  Close();
  Text(PortText(about.pszArea), UI_FACE_TEXT, Fs(20), 0.04f, kScInkNav);
  Decl note;
  note.d.layout.padding = Pad(0, 0, Fs(2), 0);
  Open(note);
  Text(PortText(about.pszNote), UI_FACE_TEXT, Fs(18), 0.0f, kScInkTip);
  Close();
  Close();
}

// The notifications (front.js 2502-2542, front.css .d-toasts): SC2's toasts, 412 units wide,
// 15 in from the right and 136 down, under the card, at most three, 5 apart; N shows and hides
// them.
void News(void)
{
  if (!g_bNewsOpen)
    return;
  const int nLeft = NotesLeft();
  if (nLeft == 0)
    return;
  Decl col;
  col.d.id = Id("toasts");
  col.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
  col.d.layout.sizing.width = Fixed(Sc(412));
  col.d.layout.childGap = Gap(Fs(5) + Fs(6));
  col.d.layout.childAlignment.x = CLAY_ALIGN_X_RIGHT;
  col.d.floating.attachTo = CLAY_ATTACH_TO_ROOT;
  col.d.floating.attachPoints.element = CLAY_ATTACH_POINT_RIGHT_TOP;
  col.d.floating.attachPoints.parent = CLAY_ATTACH_POINT_RIGHT_TOP;
  col.d.floating.offset.x = -Sc(15);
  col.d.floating.offset.y = Sc(136);
  col.d.floating.zIndex = 15;
  Open(col);
  static UiCustom toast = {0.0f, DrawToast, NULL};
  for (int i = 0; i < NotesToasts(); ++i) {
    if (NotesToastGone(i))
      continue;
    const NotesToastText n = NotesToast(i);
    const UiColor kind = n.bAnnouncement ? kTool : kScInkAccept;
    Decl it;
    it.d.id = Id("note", static_cast<int>(i));
    it.d.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    it.d.layout.sizing.width = Grow();
    it.d.layout.sizing.height = Fit(0.0f);
    it.d.layout.padding = Pad(Fs(20), Fs(52), Fs(14), Fs(14));
    it.d.custom.customData = &toast;
    Open(it);
    Decl meta;
    meta.d.layout.childGap = Gap(Fs(12));
    Open(meta);
    Text(Keep(Upper(n.tag.c_str())), UI_FACE_TEXT, Fs(14), 0.14f, kind);
    Text(Keep(Upper(n.age.c_str())), UI_FACE_TEXT, Fs(14), 0.14f, kScInkOff);
    Close();
    Decl h4;
    h4.d.layout.padding = Pad(0, 0, Fs(4), Fs(2));
    Open(h4);
    Text(n.pszTitle, UI_FACE_WIDE, Fs(20), 0.02f, kScInkTipHead);
    Close();
    Decl line;
    line.d.layout.sizing.width = Grow();
    line.d.layout.padding = Pad(0, 0, 0, Fs(6));
    Open(line);
    Text(n.pszLine, UI_FACE_TEXT, Fs(16), 0.0f, kScInkButton, -1, 1.35f);
    Close();
    const Clay_ElementId actId = Id("noteact", static_cast<int>(i));
    Decl act;
    act.d.id = actId;
    Open(act);
    const bool bActHot = Over(actId);
    if (Clicked(actId))
      NotesOpen(i);
    Text(PortText("Read the notes"), UI_FACE_TEXT, Fs(16), 0.04f, bActHot ? kScInkHi : kScInkNav, bActHot ? 0 : -1);
    Close();
    // the close button, 36 units at 8 and 8 from the corner
    Decl x = Floating("notex", static_cast<int>(i), -Sc(8), Sc(8), 16, CLAY_ATTACH_POINT_RIGHT_TOP,
                      CLAY_ATTACH_POINT_RIGHT_TOP);
    x.d.floating.attachTo = CLAY_ATTACH_TO_PARENT;
    x.d.id = Id("notex", static_cast<int>(i));
    x.d.layout.sizing.width = Fixed(Sc(36));
    x.d.layout.sizing.height = Fixed(Sc(36));
    x.d.layout.childAlignment.x = CLAY_ALIGN_X_CENTER;
    x.d.layout.childAlignment.y = CLAY_ALIGN_Y_CENTER;
    const bool bXHot = Over(x.d.id);
    if (Clicked(x.d.id))
      NotesDismiss(i);
    x.d.custom.customData = Custom(DrawScRaised, bXHot ? kScTabHi : kScTab);
    Open(x);
    Text("\xC3\x97", UI_FACE_TEXT, Fs(28), 0.0f, bXHot ? kScInkHi : kScInkButton);
    Close();
    Close();
  }
  if (nLeft > 1) {
    Decl c;
    c.d.id = Id("clearall");
    c.d.layout.sizing.height = Fixed(Sc(34));
    if (ScMini(c, PortText("Clear all"), 0))
      NotesDismissAll();
  }
  Close();
}

}  // namespace Front
