// port_hudplay.cpp -- a loaded HUD design played (port_hudplay.h), as hudplayer.js plays one.
//
// A layer's fixed fields are read into its base state once, when the design is loaded; each
// frame copies the base, reads the bound fields (port_hudbind.h) and lays the timelines' values
// over them. The transforms are CSS's: a panel moves, turns and scales about its origin, and a
// layer about its own inside it, so the matrix a layer's box is drawn with is the panel's times
// the layer's.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include "port_hudplay.h"
#include "port_hudshape.h"
#include "../front/port_ui.h"  // UiTextWidth, the faces

namespace {

const double SCREEN = 1080;
const double PAD = 72;  // the most a layer's light reaches outside its box

// hudplayer.js's saltOf: a number from a name, the seed of its noise
double SaltOf(const std::string &s) {
  int h = 0;
  for (unsigned char c : s)
    h = static_cast<int>(static_cast<unsigned int>(h) * 31u + c);
  return static_cast<double>(h % 1000) / 7.0;
}

double Noise(double n, double salt) {
  const double x = sin(n * 127.1 + salt * 311.7) * 43758.5453;
  return x - floor(x);
}

struct Jitter {
  double x = 0, y = 0, a = 1;
};

Jitter JitterOf(const HudState &s, double salt, double clock) {
  Jitter j;
  if (s.shake != 0) {
    const double n = floor(clock / 33);
    j.x = (Noise(n, salt) * 2 - 1) * s.shake;
    j.y = (Noise(n, salt + 9) * 2 - 1) * s.shake;
  }
  if (s.flicker != 0) {
    const double f = floor(clock / 45);
    if (Noise(f, salt + 3) < 0.45)
      j.a = 1 - s.flicker * (0.35 + 0.65 * Noise(f, salt + 5));
  }
  return j;
}

void SetColour(HudColour *c, const PortJson &v) {
  if (v.kind == PortJson::STRING) {
    c->bSet = HudParseColour(v.s, c->c);
    return;
  }
  if (v.kind == PortJson::NULLV || v.kind == PortJson::NONE)
    c->bSet = false;
}

void SetDefault(HudColour *c, const char *psz) {
  c->bSet = psz[0] != '\0' && HudParseColour(psz, c->c);
}

int Stops(float out[4][4], const PortJson &v) {
  if (v.kind == PortJson::STRING)
    return HudParseColour(v.s, out[0]) ? 1 : 0;
  if (v.kind != PortJson::ARRAY)
    return 0;
  int n = 0;
  for (const PortJson &c : v.items) {
    if (n == 4)
      break;
    if (c.kind == PortJson::STRING && HudParseColour(c.s, out[n]))
      ++n;
    else {
      out[n][0] = out[n][1] = out[n][2] = out[n][3] = 0;
      ++n;
    }
  }
  return n;
}

int CornerKind(const std::string &s) { return s == "chamfer" ? 1 : s == "notch" ? 2 : 0; }

void SetOpt(HudOptNum *o, const PortJson &v) {
  if (v.kind == PortJson::NUMBER) {
    o->d = v.d;
    o->bSet = true;
  } else if (v.kind == PortJson::NULLV || v.kind == PortJson::NONE) {
    o->bSet = false;
  }
}

bool Truthy(const PortJson &v) {
  switch (v.kind) {
    case PortJson::BOOL: return v.b;
    case PortJson::NUMBER: return v.d != 0.0 && v.d == v.d;
    case PortJson::STRING: return !v.s.empty();
    case PortJson::ARRAY:
    case PortJson::OBJECT: return true;
    default: return false;
  }
}

const HudState &Defaults() {
  static HudState s_state;
  static bool s_bMade = false;
  if (!s_bMade) {
    s_bMade = true;
#define HUD_COLOUR_DEFAULT(n, d) SetDefault(&s_state.n, d);
    HUD_COLOUR_FIELDS(HUD_COLOUR_DEFAULT)
#undef HUD_COLOUR_DEFAULT
  }
  return s_state;
}

// A 2D affine matrix (a, b, c, d, e, f): x' = a x + c y + e, y' = b x + d y + f.
struct Mat {
  double m[6] = {1, 0, 0, 1, 0, 0};
  static Mat Move(double x, double y) {
    Mat r;
    r.m[4] = x;
    r.m[5] = y;
    return r;
  }
  static Mat Turn(double deg) {
    Mat r;
    const double a = deg * 3.14159265358979323846 / 180;
    r.m[0] = cos(a);
    r.m[1] = sin(a);
    r.m[2] = -sin(a);
    r.m[3] = cos(a);
    return r;
  }
  static Mat Scale(double sx, double sy) {
    Mat r;
    r.m[0] = sx;
    r.m[3] = sy;
    return r;
  }
  Mat operator*(const Mat &o) const {
    Mat r;
    r.m[0] = m[0] * o.m[0] + m[2] * o.m[1];
    r.m[1] = m[1] * o.m[0] + m[3] * o.m[1];
    r.m[2] = m[0] * o.m[2] + m[2] * o.m[3];
    r.m[3] = m[1] * o.m[2] + m[3] * o.m[3];
    r.m[4] = m[0] * o.m[4] + m[2] * o.m[5] + m[4];
    r.m[5] = m[1] * o.m[4] + m[3] * o.m[5] + m[5];
    return r;
  }
};

// CSS's transform about an origin: translate(dx, dy) rotate scale, about (ox, oy)
Mat Transform(const HudState &s, double w, double h, const Jitter &j) {
  const double ox = s.origin[0] * w, oy = s.origin[1] * h;
  return Mat::Move(ox + s.dx + j.x, oy + s.dy + j.y) * Mat::Turn(s.rotate) *
         Mat::Scale(s.scale * s.scaleX, s.scale * s.scaleY) * Mat::Move(-ox, -oy);
}

// light a shape draws outside its box (its outer glow, its energy edge, its contact shadow)
// and the outline's own reach: spines, the arc, the wobble
double ShapeMargin(const HudState &s) {
  double m = 0;
  if (s.outerGlow.bSet)
    m = std::max(m, s.outerGlowSize * 3);
  if (s.energy > 0)
    m = std::max(m, s.energyWidth * 3);
  m += s.spines * 1.1 + s.bow + s.wobble * 1.2;
  if (s.castShadow > 0)
    m = std::max(m, s.elevation + s.shadowSoft * 2);
  return std::min(PAD, ceil(m + (m > 0 ? 2 : 0)));
}

void BoxOf(const HudState &s, double pw, double ph, double out[4]) {
  double x = 0, y = 0, w = pw, h = ph;
  if (s.bInset) {
    const double l = s.inset[0].bSet ? s.inset[0].d : 0, t = s.inset[1].bSet ? s.inset[1].d : 0;
    const double r = s.inset[2].bSet ? s.inset[2].d : 0, b = s.inset[3].bSet ? s.inset[3].d : 0;
    x = l;
    y = t;
    w = pw - l - r;
    h = ph - t - b;
  }
  if (s.x.bSet) x = s.x.d;
  if (s.y.bSet) y = s.y.d;
  if (s.w.bSet) w = s.w.d;
  if (s.h.bSet) h = s.h.d;
  // a null left (or top) inset with a width (or height) pins the layer to the panel's right (or
  // bottom) edge
  if (s.bInset && !s.inset[0].bSet && s.inset[2].bSet && s.w.bSet && !s.x.bSet)
    x = pw - s.inset[2].d - w;
  if (s.bInset && !s.inset[1].bSet && s.inset[3].bSet && s.h.bSet && !s.y.bSet)
    y = ph - s.inset[3].d - h;
  out[0] = x;
  out[1] = y;
  out[2] = std::max(0.0, w);
  out[3] = std::max(0.0, h);
}

// a track's value at t: the first key's before it, the last key's after it; each key's easing
// shapes the stretch that arrives at it
void Sample(const std::vector<HudTrackKey> &keys, double t, HudTrackKey *pOut) {
  if (t <= keys[0].t) {
    *pOut = keys[0];
    return;
  }
  for (size_t i = 1; i < keys.size(); ++i)
    if (t < keys[i].t) {
      const HudTrackKey &a = keys[i - 1], &b = keys[i];
      const double u = HudEase(b.nEase, (t - a.t) / std::max(1e-6, b.t - a.t));
      *pOut = a;
      if (a.s.empty() && b.s.empty() && a.bColour == b.bColour) {
        if (a.bColour)
          for (int k = 0; k < 4; ++k)
            pOut->rgba[k] = static_cast<float>(a.rgba[k] + (b.rgba[k] - a.rgba[k]) * u);
        else
          pOut->d = a.d + (b.d - a.d) * u;
      } else if (u >= 1) {
        *pOut = b;
      }
      return;
    }
  *pOut = keys.back();
}

}  // namespace

// ---------------------------------------------------------------------------
// Fields onto a state.

void HudStateSet(HudState *s, int nKey, const PortJson &v) {
  const bool bNum = v.kind == PortJson::NUMBER;
  switch (nKey) {
#define HUD_NUM_CASE(n, def) \
  case HK_##n:             \
    if (bNum)              \
      s->n = v.d;          \
    return;
    HUD_NUM_FIELDS(HUD_NUM_CASE)
#undef HUD_NUM_CASE
#define HUD_COLOUR_CASE(n, def) \
  case HK_##n:                \
    SetColour(&s->n, v);      \
    return;
    HUD_COLOUR_FIELDS(HUD_COLOUR_CASE)
#undef HUD_COLOUR_CASE
    case HK_x: SetOpt(&s->x, v); return;
    case HK_y: SetOpt(&s->y, v); return;
    case HK_w: SetOpt(&s->w, v); return;
    case HK_h: SetOpt(&s->h, v); return;
    case HK_scan: SetOpt(&s->scan, v); return;
    case HK_lit: SetOpt(&s->lit, v); return;
    case HK_pathScale: SetOpt(&s->pathScale, v); return;
    case HK_visible: s->bVisible = Truthy(v); return;
    case HK_enabled: s->bEnabled = Truthy(v); return;
    case HK_label: s->label = v.kind == PortJson::STRING ? v.s : std::string(); return;
    case HK_origin:
      if (v.kind == PortJson::ARRAY && v.size() >= 2) {
        s->origin[0] = v.At(0).Num(0.5);
        s->origin[1] = v.At(1).Num(0.5);
      }
      return;
    case HK_fill: s->nFill = Stops(s->fill, v); return;
    case HK_stroke: s->nStroke = Stops(s->stroke, v); return;
    case HK_corners:
      if (bNum) {
        for (double &c : s->corners) c = v.d;
      } else if (v.kind == PortJson::ARRAY && v.size() > 0) {
        for (int i = 0; i < 4; ++i)
          s->corners[i] = v.At(std::min<size_t>(i, v.size() - 1)).Num();
      }
      return;
    case HK_cornerStyle:
      if (v.kind == PortJson::STRING) {
        for (int &k : s->cornerKinds) k = CornerKind(v.s);
      } else if (v.kind == PortJson::ARRAY && v.size() > 0) {
        for (int i = 0; i < 4; ++i)
          s->cornerKinds[i] = CornerKind(v.At(std::min<size_t>(i, v.size() - 1)).Str());
      }
      return;
    case HK_cutouts:
      s->nCutouts = 0;
      for (const PortJson &c : v.items) {
        if (s->nCutouts == 4)
          break;
        for (int k = 0; k < 5; ++k)
          s->cutouts[s->nCutouts][k] = c.At(k).Num();
        ++s->nCutouts;
      }
      return;
    case HK_inset:
      s->bInset = v.kind == PortJson::ARRAY && v.size() == 4;
      for (int i = 0; i < 4 && s->bInset; ++i)
        SetOpt(&s->inset[i], v.At(i));
      return;
    case HK_shadowOffset:
      if (v.kind == PortJson::ARRAY && v.size() >= 2) {
        s->shadowOffset[0] = v.At(0).Num();
        s->shadowOffset[1] = v.At(1).Num();
      }
      return;
    case HK_glowMode: s->nGlowMode = v.Str() == "over" ? 1 : 0; return;
    case HK_scanMode: s->nScanMode = v.Str() == "lines" ? 1 : 0; return;
    case HK_blend: s->nBlend = v.Str() == "add" ? 1 : 0; return;
    case HK_meterFrom: {
      const std::string m = v.Str();
      s->nMeterFrom = m == "right" ? 1 : m == "top" ? 2 : m == "bottom" ? 3 : 0;
      return;
    }
    case HK_align: {
      const std::string a = v.Str();
      s->nAlign = a == "center" ? 1 : a == "right" ? 2 : 0;
      s->bAlignSet = v.kind == PortJson::STRING;
      return;
    }
    case HK_valign: {
      const std::string a = v.Str();
      s->nValign = a == "top" ? 0 : a == "bottom" ? 2 : 1;
      return;
    }
    case HK_wrap:
      s->wrap = v.kind == PortJson::BOOL ? (v.b ? -1 : 0) : bNum ? v.d : 0;
      return;
    case HK_font:
      if (v.kind == PortJson::STRING)
        s->font = v.s;
      return;
    case HK_surface: {
      const int k = HudSurfaceOf(v.Str("none"));
      s->nSurface = k < 0 ? 0 : k;
      s->bSurfaceLive = HudSurfaceLive(v.Str());
      return;
    }
    case HK_profile: {
      const int k = HudProfileOf(v.Str("slope"));
      s->nProfile = k < 0 ? 0 : k;
      return;
    }
    case HK_pattern: {
      const int k = HudPatternOf(v.Str("none"));
      s->nPattern = k < 0 ? 0 : k;
      return;
    }
    case HK_boltStyle: {
      const int k = HudBoltStyleOf(v.Str("dome"));
      s->nBoltStyle = k < 0 ? 0 : k;
      return;
    }
    case HK_revealStyle: s->nRevealStyle = v.Str() == "warp" ? 1 : 0; return;
    case HK_mirror: s->bMirror = Truthy(v); return;
    case HK_axis: s->nAxis = v.Str() == "y" ? 1 : 0; return;
    case HK_tileFit: {
      const std::string f = v.Str();
      s->nTileFit = f == "start" ? 1 : f == "round" ? 2 : 0;
      return;
    }
    case HK_pathFit: {
      const std::string f = v.Str();
      s->nPathFit = f == "stretch" ? 1 : f == "fit" ? 2 : 0;
      return;
    }
    case HK_path: s->pPath = v.kind == PortJson::STRING ? &v.s : nullptr; return;
    case HK_pathSize:
      s->bPathSize = v.kind == PortJson::ARRAY && v.size() == 2;
      if (s->bPathSize) {
        s->pathSize[0] = v.At(0).Num();
        s->pathSize[1] = v.At(1).Num();
      }
      return;
    case HK_pathSlice:
      s->nPathSlice = v.kind == PortJson::ARRAY && (v.size() == 2 || v.size() == 4) ? static_cast<int>(v.size()) : 0;
      for (int i = 0; i < s->nPathSlice; ++i)
        s->pathSlice[i] = v.At(i).Num();
      return;
    default:
      return;  // read as a whole from the layer's def where it is used (places, grid, run, regions)
  }
}

void HudStateSetVal(HudState *s, int nKey, const HudVal &v) {
  if (nKey == HK_path)
    return;
  PortJson j;
  if (v.kind == HudVal::NUM) {
    j.kind = PortJson::NUMBER;
    j.d = v.d;
  } else if (v.kind == HudVal::BOOL) {
    j.kind = PortJson::BOOL;
    j.b = v.b;
  } else {
    j.kind = PortJson::STRING;
    j.s = v.s;
  }
  HudStateSet(s, nKey, j);
}

void HudStateSetKey(HudState *s, int nKey, const HudTrackKey &v) {
  if (v.bColour) {
    HudColour c;
    memcpy(c.c, v.rgba, sizeof c.c);
    c.bSet = true;
    switch (nKey) {
#define HUD_COLOUR_KEY(n, def) \
  case HK_##n:               \
    s->n = c;                \
    return;
      HUD_COLOUR_FIELDS(HUD_COLOUR_KEY)
#undef HUD_COLOUR_KEY
      case HK_fill:
        s->nFill = 1;
        memcpy(s->fill[0], v.rgba, sizeof s->fill[0]);
        return;
      case HK_stroke:
        s->nStroke = 1;
        memcpy(s->stroke[0], v.rgba, sizeof s->stroke[0]);
        return;
      default:
        return;
    }
  }
  PortJson j;
  if (v.s.empty()) {
    j.kind = PortJson::NUMBER;
    j.d = v.d;
  } else {
    j.kind = PortJson::STRING;
    j.s = v.s;
  }
  if (nKey != HK_path)
    HudStateSet(s, nKey, j);
}

// ---------------------------------------------------------------------------
// The player.

int HudFaceOf(const std::string &font, double weight) {
  if (font == "chakra-petch")
    return weight >= 650   ? UI_FACE_PETCH_BOLD
           : weight >= 550 ? UI_FACE_PETCH_SEMI
           : weight >= 450 ? UI_FACE_PETCH_MEDIUM
                           : UI_FACE_PETCH;
  if (font == "eurostile-ext" || font == "michroma")
    return UI_FACE_WIDE;
  return weight >= 550 ? UI_FACE_BOLD : UI_FACE_TEXT;
}

void HudPlayer::Load(const HudDesign *pDesign) {
  m_pDesign = pDesign;
  m_insts.clear();
  m_cues.clear();
  m_order = 0;
  m_bTipNow = m_bTipShown = false;
  m_tipHideAt = -1;
  m_nTipPanel = m_nTipPlate = m_nTipPane = m_nTipMarkLine = -1;
  const HudDesign &d = *pDesign;
  m_nMusicPanel = -1;
  m_musicLayers.clear();
  for (std::vector<std::vector<MusicBox>> &w : m_musicPlates)
    w.assign(HUD_MUSIC_PARTS, {});
  const char *const *kParts = kHudMusicParts;
  for (size_t p = 0; p < d.panels.size(); ++p)
    if (d.panels[p].bMusic) {
      m_nMusicPanel = static_cast<int>(p);
      // "w<window>-<part>-<n>", or the menu's "menu-<n>"
      for (const HudLayerDef &ld : d.panels[p].layers) {
        MusicAt at = {HUD_MUSIC_WINDOWS, -1, 0};
        std::string id = ld.id;
        if (id.size() > 3 && id[0] == 'w' && id[2] == '-') {
          at.nWin = id[1] - '0';
          id = id.substr(3);
        }
        const size_t dash = id.rfind('-');
        for (int k = 0; k < HUD_MUSIC_PARTS && dash != std::string::npos; ++k)
          if (id.compare(0, dash, kParts[k]) == 0)
            at.nPart = k;
        at.nIndex = dash != std::string::npos ? atoi(id.c_str() + dash + 1) : 0;
        if (at.nWin < 0 || at.nWin > HUD_MUSIC_WINDOWS)
          at.nPart = -1;
        m_musicLayers.push_back(at);
      }
    }
  for (size_t p = 0; p < d.panels.size() && d.tooltip.bOn; ++p)
    if (d.panels[p].bTooltip) {
      m_nTipPanel = static_cast<int>(p);
      for (size_t l = 0; l < d.panels[p].layers.size(); ++l) {
        const std::string &id = d.panels[p].layers[l].id;
        (id == "plate" ? m_nTipPlate : id == "pane" ? m_nTipPane : id == "markLine" ? m_nTipMarkLine : m_order) =
            static_cast<int>(l);
      }
      m_order = 0;
      m_tipParts.assign(d.panels[p].layers.size(), TipPart());
      m_tipHas.assign(d.panels[p].layers.size(), false);
    }
  m_panelBase.assign(d.panels.size(), Defaults());
  m_panelState = m_panelBase;
  m_layerBase.assign(d.panels.size(), {});
  m_layerState.assign(d.panels.size(), {});
  m_tileStates.assign(d.panels.size(), {});
  m_panelOver.assign(d.panels.size(), {});
  m_layerOver.assign(d.panels.size(), {});
  HudEnv none;
  for (size_t p = 0; p < d.panels.size(); ++p) {
    const HudPanelDef &pd = d.panels[p];
    HudState &ps = m_panelBase[p];
    for (const HudField &f : pd.fields)
      if (f.nTemplate < 0)
        HudStateSet(&ps, f.nKey, f.value);
    m_layerBase[p].assign(pd.layers.size(), Defaults());
    m_layerOver[p].assign(pd.layers.size(), {});
    for (size_t l = 0; l < pd.layers.size(); ++l) {
      const HudLayerDef &ld = pd.layers[l];
      HudState &ls = m_layerBase[p][l];
      for (const HudField &f : ld.fields)
        if (f.nTemplate < 0)
          HudStateSet(&ls, f.nKey, f.value);
    }
    m_layerState[p] = m_layerBase[p];
  }
  // each track's target, found once
  m_trackTarget.assign(d.timelines.size(), {});
  for (size_t t = 0; t < d.timelines.size(); ++t)
    for (const HudTrack &tr : d.timelines[t].tracks) {
      std::pair<int, int> at(-1, -1);
      const size_t dot = tr.target.find('.');
      const std::string pid = tr.target.substr(0, dot);
      const int nPanel = PanelIndex(pid);
      if (nPanel >= 0 && tr.nProp >= 0) {
        if (dot == std::string::npos) {
          at = {nPanel, -1};
        } else {
          const std::string lid = tr.target.substr(dot + 1);
          const HudPanelDef &pd = d.panels[nPanel];
          for (size_t l = 0; l < pd.layers.size(); ++l)
            if (pd.layers[l].id == lid)
              at = {nPanel, static_cast<int>(l)};
        }
      }
      m_trackTarget[t].push_back(at);
    }
  PlaceAll();
}

int HudPlayer::PanelIndex(const std::string &id) const {
  if (m_pDesign == nullptr)
    return -1;
  for (size_t p = 0; p < m_pDesign->panels.size(); ++p)
    if (m_pDesign->panels[p].id == id)
      return static_cast<int>(p);
  return -1;
}

void HudPlayer::Layout(int nWidth, int nHeight) {
  m_lw = nHeight > 0 ? SCREEN * nWidth / nHeight : 1920;
  m_px = nHeight > 0 ? nHeight / SCREEN : 1;
  PlaceAll();
  if (m_bTipShown)
    LayTip();
}

// The band the panels are laid out in (hudplayer.js's bandOf): the design's band in the
// middle of a wider screen, else the screen. Every anchor, "between" end and clamp means its
// edges; a panel "outside" it fills the margin on its side, 0 wide with no margin.
void HudPlayer::Band(double *pfLeft, double *pfWidth) const {
  const double b = m_pDesign != nullptr ? m_pDesign->band : 0;
  *pfLeft = b > 0 && m_lw > b ? (m_lw - b) / 2 : 0;
  *pfWidth = b > 0 && m_lw > b ? b : m_lw;
}

void HudPlayer::Sides(double *pfLeft, double *pfRight) const {
  *pfLeft = *pfRight = 0;
  if (m_pDesign == nullptr || m_rects.size() != m_pDesign->panels.size())
    return;
  const double give = 1.0;  // a unit either way
  std::vector<const std::vector<double> *> tall;
  for (size_t p = 0; p < m_rects.size(); ++p) {
    const std::vector<double> &r = m_rects[p];
    if (!m_pDesign->panels[p].bTooltip && !m_pDesign->panels[p].bMusic && r[2] > 0 && r[1] <= give &&
        r[1] + r[3] >= SCREEN - give)
      tall.push_back(&r);
  }
  double left = 0, right = 0;
  for (bool bGrew = true; bGrew;) {
    bGrew = false;
    for (const std::vector<double> *r : tall) {
      const double x0 = (*r)[0], x1 = (*r)[0] + (*r)[2];
      if (x0 <= left + give && x1 > left + give) {
        left = x1;
        bGrew = true;
      }
      if (x1 >= m_lw - right - give && x0 < m_lw - right - give) {
        right = m_lw - x0;
        bGrew = true;
      }
    }
  }
  // a design tall at both sides that meet leaves the map nothing: none
  if (left + right >= m_lw - give)
    return;
  *pfLeft = left;
  *pfRight = right;
}

// every panel's box, in file order, so a panel between two others finds theirs
void HudPlayer::PlaceAll() {
  if (m_pDesign == nullptr)
    return;
  const HudDesign &d = *m_pDesign;
  double bandLeft, bandW;
  Band(&bandLeft, &bandW);
  m_rects.assign(d.panels.size(), std::vector<double>(4, 0.0));
  for (size_t p = 0; p < d.panels.size(); ++p) {
    const HudPanelDef &pd = d.panels[p];
    // the tooltip goes where LayTip puts it, which is on the screen, as the music player's plates
    const bool bScreen = pd.bTooltip || pd.bMusic;
    const double bl = bScreen ? 0 : bandLeft, bw = bScreen ? m_lw : bandW, br = bl + bw;
    double w = pd.w, h = pd.h, left;
    if (pd.nOutside != 0) {
      const double from = pd.nOutside == 1 ? 0 : br, to = pd.nOutside == 1 ? bl : m_lw;
      if (to - from > 0) {
        left = from - pd.overlap[0];
        w = to - from + pd.overlap[0] + pd.overlap[1];
      } else {
        left = from;
        w = 0;
      }
    } else if (pd.bBetween) {
      const int a = PanelIndex(pd.between[0]), b = PanelIndex(pd.between[1]);
      const double from = pd.between[0] == "left" ? bl : a >= 0 && a < static_cast<int>(p) ? m_rects[a][0] + m_rects[a][2] : bl;
      const double to = pd.between[1] == "right" ? br : b >= 0 && b < static_cast<int>(p) ? m_rects[b][0] : br;
      left = from - pd.overlap[0];
      w = std::max(0.0, to + pd.overlap[1] - left);
    } else {
      left = bl + (pd.nAx == 0 ? pd.x : pd.nAx == 2 ? bw - pd.x - w : (bw - w) / 2 + pd.x);
    }
    double top = pd.nAy == 0 ? pd.y : pd.nAy == 2 ? SCREEN - pd.y - h : (SCREEN - h) / 2 + pd.y;
    if (pd.bClamp && pd.nOutside == 0) {
      left = std::max(bl, std::min(br - w, left));
      top = std::max(0.0, std::min(SCREEN - h, top));
    }
    m_rects[p] = {left, top, w, h};
    if (pd.bMusic)
      m_rects[p].assign(m_musicRect, m_musicRect + 4);
  }
}

bool HudPlayer::PanelRect(int nPanel, double out[4]) const {
  if (nPanel < 0 || nPanel >= static_cast<int>(m_rects.size()))
    return false;
  for (int i = 0; i < 4; ++i)
    out[i] = m_rects[nPanel][i];
  return true;
}

int HudPlayer::Fire(int nEvent, const std::string &panel, int nSlot, int nGroup, int nView, int nAction,
                    int nActionArg) {
  if (m_pDesign == nullptr)
    return 0;
  if (nEvent == HUD_EV_GAME_START)
    m_insts.clear();
  int n = 0;
  const std::vector<HudTimeline> &tls = m_pDesign->timelines;
  for (size_t t = 0; t < tls.size(); ++t) {
    const HudTimeline &tl = tls[t];
    if (tl.nOn != nEvent)
      continue;
    if ((nEvent == HUD_EV_PANEL_HOVER || nEvent == HUD_EV_PANEL_LEAVE || nEvent == HUD_EV_PANEL_PRESS) && tl.panel != panel)
      continue;
    if (nEvent == HUD_EV_PANEL_PRESS && tl.nSlot >= 0 && tl.nSlot != nSlot)
      continue;
    if (nEvent == HUD_EV_PANEL_PRESS && tl.nAction >= 0 && (tl.nAction != nAction || tl.nActionArg != nActionArg))
      continue;
    if ((nEvent == HUD_EV_GROUP_RECALL || nEvent == HUD_EV_GROUP_SET) && tl.nGroup >= 0 && tl.nGroup != nGroup)
      continue;
    if ((nEvent == HUD_EV_STATS_SHOW || nEvent == HUD_EV_STATS_HIDE) && tl.nView >= 0 && tl.nView != nView)
      continue;
    // "stops" ends other timelines, held ones too: console.in ends console.out
    m_insts.erase(std::remove_if(m_insts.begin(), m_insts.end(),
                                 [&](const Inst &x) {
                                   return x.nTl == static_cast<int>(t) ||
                                          std::find(tl.stops.begin(), tl.stops.end(), x.nTl) != tl.stops.end();
                                 }),
                  m_insts.end());
    m_insts.push_back({static_cast<int>(t), 0.0, tl.duration, m_order++, false});
    ++n;
  }
  return n;
}

void HudPlayer::Advance(double dtMs) {
  m_cues.clear();
  if (m_pDesign == nullptr)
    return;
  m_clock += dtMs;
  const std::vector<HudTimeline> &tls = m_pDesign->timelines;
  auto cues = [&](const HudTimeline &tl, double from, double to) {
    for (const HudCue &c : tl.cues)
      if (c.t >= from && c.t < to)
        m_cues.push_back({c.asset, c.volume});
  };
  std::vector<Inst> kept;
  for (Inst x : m_insts) {
    const HudTimeline &tl = tls[x.nTl];
    const double was = x.t;
    x.t += dtMs;
    cues(tl, x.cued ? was : -1, x.t + (x.t >= x.dur ? 1 : 0));
    x.cued = true;
    if (x.t < x.dur) {
      kept.push_back(x);
      continue;
    }
    if (tl.nEnd == HUD_END_LOOP && x.dur > 0) {
      x.t = fmod(x.t, x.dur);
      cues(tl, -1, x.t);
      kept.push_back(x);
    } else if (tl.nEnd == HUD_END_HOLD) {
      x.t = x.dur;
      kept.push_back(x);
    }
  }
  m_insts = kept;
  // a tooltip going is hidden once its tip.hide timelines have run
  if (m_tipHideAt >= 0 && m_clock >= m_tipHideAt) {
    m_bTipShown = false;
    m_tipHideAt = -1;
    m_insts.erase(std::remove_if(m_insts.begin(), m_insts.end(),
                                 [&](const Inst &x) { return tls[x.nTl].nOn == HUD_EV_TIP_HIDE; }),
                  m_insts.end());
  }
}

std::vector<std::pair<std::string, double>> HudPlayer::Running() const {
  std::vector<std::pair<std::string, double>> out;
  if (m_pDesign != nullptr)
    for (const Inst &x : m_insts)
      out.push_back({m_pDesign->timelines[x.nTl].id, x.t});
  return out;
}

void HudPlayer::Overrides() {
  for (auto &v : m_panelOver)
    v.clear();
  for (auto &p : m_layerOver)
    for (auto &v : p)
      v.clear();
  std::vector<const Inst *> order;
  for (const Inst &x : m_insts)
    order.push_back(&x);
  std::sort(order.begin(), order.end(), [](const Inst *a, const Inst *b) { return a->order < b->order; });
  for (const Inst *x : order) {
    const HudTimeline &tl = m_pDesign->timelines[x->nTl];
    for (size_t k = 0; k < tl.tracks.size(); ++k) {
      const std::pair<int, int> at = m_trackTarget[x->nTl][k];
      if (at.first < 0)
        continue;
      const HudTrack &tr = tl.tracks[k];
      if (tr.keys.empty())
        continue;
      Over o;
      o.nProp = tr.nProp;
      Sample(tr.keys, x->t - tr.delay, &o.v);
      std::vector<Over> &list = at.second < 0 ? m_panelOver[at.first] : m_layerOver[at.first][at.second];
      bool bFound = false;
      for (Over &e : list)
        if (e.nProp == o.nProp) {
          e = o;
          bFound = true;
        }
      if (!bFound)
        list.push_back(o);
    }
  }
}

const std::vector<HudDrawItem> &HudPlayer::Frame(const HudEnv &env) {
  m_draw.clear();
  if (m_pDesign == nullptr)
    return m_draw;
  const HudDesign &d = *m_pDesign;
  Overrides();
  m_panels.assign(d.panels.size(), HudPanelFrame());
  for (size_t p = 0; p < d.panels.size(); ++p) {
    const HudPanelDef &pd = d.panels[p];
    HudState &ps = m_panelState[p];
    ps = m_panelBase[p];
    for (const HudField &f : pd.fields)
      if (f.nTemplate >= 0)
        HudStateSetVal(&ps, f.nKey, d.bindings.Eval(f.nTemplate, env));
    const bool bTip = static_cast<int>(p) == m_nTipPanel;
    if (bTip)
      ps.bVisible = m_bTipShown;
    for (const Over &o : m_panelOver[p]) {
      if (o.nProp == HK_x)
        ps.dx = o.v.d;
      else if (o.nProp == HK_y)
        ps.dy = o.v.d;
      else
        HudStateSetKey(&ps, o.nProp, o.v);
    }
    if (!ps.bVisible)
      continue;
    const std::vector<double> &r = m_rects[p];
    // a panel outside the band is not drawn while its margin is empty; its timelines run on
    if (pd.nOutside != 0 && !(r[2] > 0))
      continue;
    const Jitter pjs = JitterOf(ps, SaltOf(pd.id), m_clock);
    const Mat P = Mat::Move(r[0], r[1]) * Transform(ps, r[2], r[3], pjs);
    const double panelAlpha = std::max(0.0, std::min(1.0, ps.opacity * pjs.a));
    m_panels[p].bVisible = true;
    memcpy(m_panels[p].m, P.m, sizeof P.m);
    m_panels[p].opacity = panelAlpha;
    std::vector<HudState> &tiles = m_tileStates[p];
    tiles.clear();
    // the copies' states are pointed at, so the list is made in full before the items are
    size_t nTiles = 0;
    for (size_t l = 0; l < pd.layers.size(); ++l)
      nTiles += m_layerBase[p][l].tileEvery > 0 ? 400 : 0;
    tiles.reserve(nTiles);
    for (size_t l = 0; l < pd.layers.size(); ++l) {
      const HudLayerDef &ld = pd.layers[l];
      HudState &ls = m_layerState[p][l];
      ls = m_layerBase[p][l];
      for (const HudField &f : ld.fields)
        if (f.nTemplate >= 0)
          HudStateSetVal(&ls, f.nKey, d.bindings.Eval(f.nTemplate, env));
      if (bTip)
        TipLayer(static_cast<int>(l), &ls);
      else if (static_cast<int>(p) == m_nMusicPanel)
        MusicLayer(static_cast<int>(l), &ls);
      for (const Over &o : m_layerOver[p][l]) {
        if (o.nProp == HK_x)
          ls.dx = o.v.d;
        else if (o.nProp == HK_y)
          ls.dy = o.v.d;
        else
          HudStateSetKey(&ls, o.nProp, o.v);
      }
      if (ld.nText >= 0 && !bTip)
        ls.text = HudShow(d.bindings.Eval(ld.nText, env));
      double box[4];
      BoxOf(ls, r[2], r[3], box);
      // a layer riding another's meter, centred on its lit end across its middle, with
      // the tracks' move of the layer ridden (hudplayer.js's rideBox)
      if (l == 0)
        m_boxes.assign(pd.layers.size() * 4, 0.0);
      if (ld.nRide >= 0 && ld.nRide < static_cast<int>(l)) {
        const HudState &rs = m_layerState[p][ld.nRide];
        const double *rb = &m_boxes[ld.nRide * 4];
        const double f = std::max(0.0, std::min(1.0, rs.meter));
        double cx, cy;
        if (rs.nMeterFrom >= 2) {
          cx = rb[0] + rb[2] / 2;
          cy = rb[1] + rb[3] * (rs.nMeterFrom == 2 ? f : 1 - f);
        } else {
          cy = rb[1] + rb[3] / 2;
          cx = rb[0] + rb[2] * (rs.nMeterFrom == 0 ? f : 1 - f);
        }
        box[0] = cx - box[2] / 2 + rs.dx;
        box[1] = cy - box[3] / 2 + rs.dy;
      }
      memcpy(&m_boxes[l * 4], box, sizeof box);
      if (!ls.bVisible || ls.opacity <= 0)
        continue;
      // the each, icon and chart layers are read and not drawn yet
      if (ld.kind == HUD_EACH || ld.kind == HUD_ICON || ld.kind == HUD_CHART)
        continue;
      const Jitter lj = JitterOf(ls, ld.fSalt, m_clock);
      double margin = 0;
      if (ld.kind == HUD_SHAPE)
        margin = ShapeMargin(ls);
      auto emit = [&](const HudState *s, const double *b) {
        HudDrawItem it;
        it.nPanel = static_cast<int>(p);
        it.nLayer = static_cast<int>(l);
        it.kind = ld.kind;
        it.nSlot = ld.nSlot;
        it.pState = s;
        memcpy(it.box, b, sizeof it.box);
        it.margin = margin;
        it.screenAt[0] = r[0] + ps.dx + b[0] + s->dx;
        it.screenAt[1] = r[1] + ps.dy + b[1] + s->dy;
        const Mat L = Mat::Move(b[0], b[1]) * Transform(*s, b[2], b[3], lj);
        const Mat M = P * L;
        memcpy(it.m, M.m, sizeof it.m);
        memcpy(it.ml, L.m, sizeof it.ml);
        it.layerOpacity = std::max(0.0, std::min(1.0, s->opacity * lj.a));
        it.opacity = panelAlpha * it.layerOpacity;
        m_draw.push_back(it);
      };
      if (!(ls.tileEvery > 0) || ld.kind == HUD_SLOT) {
        emit(&ls, box);
        continue;
      }
      // a layer repeated along its panel, tileEvery units apart, from its box to tileEnd short of
      // the far edge: center as many whole copies as fit centred, start the same from the box,
      // round a whole number filling the span, each lengthened or shortened by what is over
      const bool bY = ls.nAxis == 1;
      const double every = ls.tileEvery;
      const double at0 = bY ? box[1] : box[0], len = bY ? box[3] : box[2];
      const double span = (bY ? r[3] : r[2]) - ls.tileEnd - at0;
      int n;
      double step, size = len, off = 0;
      if (ls.nTileFit == 2) {
        n = std::max(1, static_cast<int>(floor(span / every + 0.5)));
        step = span / n;
        size = std::max(1.0, len + step - every);
      } else {
        n = span >= len ? static_cast<int>(floor((span - len) / every)) + 1 : 0;
        step = every;
        if (ls.nTileFit != 1)
          off = (span - ((n - 1) * every + len)) / 2;
      }
      for (int i = 0; i < std::min(n, 400); ++i) {
        const double dd = off + i * step;
        double b[4] = {box[0], box[1], box[2], box[3]};
        if (bY) {
          b[1] += dd;
          b[3] = size;
        } else {
          b[0] += dd;
          b[2] = size;
        }
        tiles.push_back(ls);
        emit(&tiles.back(), b);
      }
    }
  }
  return m_draw;
}

// ---------------------------------------------------------------------------
// A state's hash (FNV-1a over its values, field by field, so padding never counts).

namespace {

struct Fnv {
  unsigned long long h = 1469598103934665603ull;
  void Bytes(const void *p, size_t n) {
    const unsigned char *b = static_cast<const unsigned char *>(p);
    for (size_t i = 0; i < n; ++i)
      h = (h ^ b[i]) * 1099511628211ull;
  }
  void D(double d) { Bytes(&d, sizeof d); }
  void I(int n) { Bytes(&n, sizeof n); }
  void C(const HudColour &c) {
    I(c.bSet ? 1 : 0);
    if (c.bSet)
      Bytes(c.c, sizeof c.c);
  }
  void O(const HudOptNum &o) {
    I(o.bSet ? 1 : 0);
    if (o.bSet)
      D(o.d);
  }
  void S(const std::string &s) {
    I(static_cast<int>(s.size()));
    Bytes(s.data(), s.size());
  }
};

}  // namespace

unsigned long long HudStateHash(const HudState &s, bool bPlacing) {
  Fnv f;
  f.I(s.bVisible ? 1 : 0);
  f.O(s.x);
  f.O(s.y);
  f.O(s.w);
  f.O(s.h);
  // what only places the layer counts as nothing without bPlacing
  const double *const apPlacing[] = {&s.opacity, &s.scale, &s.scaleX, &s.scaleY, &s.rotate, &s.flicker, &s.shake};
  auto placing = [&](const double *p) {
    for (const double *q : apPlacing)
      if (p == q)
        return true;
    return false;
  };
#define HUD_NUM_HASH(n, def) f.D(!bPlacing && placing(&s.n) ? 0.0 : s.n);
  HUD_NUM_FIELDS(HUD_NUM_HASH)
#undef HUD_NUM_HASH
#define HUD_COLOUR_HASH(n, def) f.C(s.n);
  HUD_COLOUR_FIELDS(HUD_COLOUR_HASH)
#undef HUD_COLOUR_HASH
  f.O(s.scan);
  f.O(s.lit);
  f.O(s.pathScale);
  f.I(s.nFill);
  f.Bytes(s.fill, sizeof s.fill);
  f.I(s.nStroke);
  f.Bytes(s.stroke, sizeof s.stroke);
  f.Bytes(s.corners, sizeof s.corners);
  f.Bytes(s.cornerKinds, sizeof s.cornerKinds);
  f.I(s.nCutouts);
  f.Bytes(s.cutouts, sizeof s.cutouts);
  f.I(s.bInset ? 1 : 0);
  for (const HudOptNum &o : s.inset)
    f.O(o);
  f.Bytes(s.shadowOffset, sizeof s.shadowOffset);
  const int an[] = {s.nGlowMode, s.nScanMode, s.nBlend, s.nMeterFrom, s.nAlign, s.bAlignSet ? 1 : 0, s.nValign,
                    s.nSurface, s.bSurfaceLive ? 1 : 0, s.nProfile, s.nPattern, s.nBoltStyle, s.nRevealStyle,
                    s.bMirror ? 1 : 0, s.nAxis, s.nTileFit, s.nPathFit, s.bPathSize ? 1 : 0, s.nPathSlice};
  f.Bytes(an, sizeof an);
  f.D(s.wrap);
  f.S(s.pPath != nullptr ? *s.pPath : std::string());  // the tooltip's changes in place
  f.Bytes(s.pathSize, sizeof s.pathSize);
  f.Bytes(s.pathSlice, sizeof s.pathSlice);
  f.S(s.font);
  f.S(s.text);
  if (bPlacing) {
    f.D(s.dx);
    f.D(s.dy);
    f.Bytes(s.origin, sizeof s.origin);
  }
  return f.h;
}

// ---------------------------------------------------------------------------
// The tooltip: hudplayer.js's tipLayout, tipPlace, tipPath and p.tip.

namespace {

std::string TipClean(const std::string &s) {
  std::string out;
  for (char c : s)
    if (c != '{' && c != '}')
      out += c;
  return out;
}

// a text's width in units in its face, measured at the pixels it is drawn at
double Measure(const HudState &s, const std::string &text, double px) {
  if (text.empty())
    return 0;
  const UiFace face = static_cast<UiFace>(HudFaceOf(s.font, s.weight));
  return UiTextWidth(face, static_cast<float>(s.size * px), static_cast<float>(s.tracking * px), text.c_str()) / px;
}

// a text broken into lines no wider than w: at its newlines, then between words
std::vector<std::string> WrapText(const HudState &s, const std::string &text, double w, double px) {
  std::vector<std::string> lines;
  size_t at = 0;
  for (;;) {
    const size_t nl = text.find('\n', at);
    const std::string para = text.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
    std::string cur;
    size_t i = 0;
    while (i < para.size()) {
      while (i < para.size() && isspace(static_cast<unsigned char>(para[i])))
        ++i;
      size_t j = i;
      while (j < para.size() && !isspace(static_cast<unsigned char>(para[j])))
        ++j;
      if (j == i)
        break;
      const std::string word = para.substr(i, j - i);
      const std::string tryS = cur.empty() ? word : cur + " " + word;
      if (!cur.empty() && Measure(s, tryS, px) > w) {
        lines.push_back(cur);
        cur = word;
      } else {
        cur = tryS;
      }
      i = j;
    }
    lines.push_back(cur);
    if (nl == std::string::npos)
      break;
    at = nl + 1;
  }
  return lines;
}

std::string PathNum(double v) {
  char sz[32];
  snprintf(sz, sizeof sz, "%g", floor(v * 100 + 0.5) / 100);
  return sz;
}

}  // namespace

bool HudPlayer::LayTip() {
  const int p = m_nTipPanel;
  if (p < 0 || !m_bTipNow)
    return false;
  const HudTooltip &T = m_pDesign->tooltip;
  const HudPanelDef &pd = m_pDesign->panels[p];
  const std::vector<HudState> &base = m_layerBase[p];
  auto index = [&](const std::string &id) {
    for (size_t l = 0; l < pd.layers.size(); ++l)
      if (pd.layers[l].id == id)
        return static_cast<int>(l);
    return -1;
  };
  auto dressed = [&](const char *pszId) -> const HudState & {
    const int l = index(pszId);
    return l >= 0 ? base[l] : Defaults();
  };
  const HudState &Dt = dressed("title"), &Dk = dressed("keyText"), &Dc = dressed("costMinerals"), &Dl = dressed("line1");
  const HudTipContent &c = m_tip;
  const double f = T.frame, px = m_px;
  const double *pad = T.pad;
  std::map<std::string, TipPart> parts;
  auto part = [&](const std::string &id, double x, double y, double w, double h) -> TipPart & {
    TipPart &q = parts[id];
    q.b[0] = x, q.b[1] = y, q.b[2] = w, q.b[3] = h;
    return q;
  };
  auto text = [](TipPart &q, const std::string &s) {
    q.bText = true;
    q.text = s;
  };
  const std::string title = TipClean(c.title), hot = TipClean(c.hotkey);
  const int mark = c.nMark >= 0 && c.nMark < static_cast<int>(title.size()) ? c.nMark : -1;
  double rowT = Dt.size * Dt.lineHeight, keyW = 0, keyH = 0;
  if (!hot.empty()) {
    keyH = Dk.size + 2 * T.keyPad[1];
    keyW = std::max(keyH, Measure(Dk, hot, px) + 2 * T.keyPad[0]);
    rowT = std::max(rowT, keyH);
  }
  const double tw = Measure(Dt, title, px);
  std::vector<int> costs;
  double cw = 0;
  for (int k = 0; k < HUD_TIP_COSTS; ++k)
    if (!c.costs[k].empty()) {
      cw += (costs.empty() ? 0 : T.costGap) + T.icon + T.iconGap + Measure(Dc, TipClean(c.costs[k]), px);
      costs.push_back(k);
    }
  const double costH = costs.empty() ? 0 : std::max(T.icon, Dc.size * 1.2);
  const double room = std::max(40.0, T.maxWidth - 2 * f - pad[0] - pad[2]);
  std::vector<std::string> lines;
  for (size_t i = 0; i < c.lines.size() && lines.size() < HUD_TIP_LINES; ++i)
    if (!TipClean(c.lines[i]).empty())
      lines.push_back(TipClean(c.lines[i]));
  std::vector<std::vector<std::string>> wrapped;
  double lw = 0;
  for (const std::string &s : lines) {
    wrapped.push_back(WrapText(Dl, s, room, px));
    for (const std::string &ln : wrapped.back())
      lw = std::max(lw, Measure(Dl, ln, px));
  }
  const double inner = ceil(std::max(std::max(tw + (hot.empty() ? 0 : T.keyGap + keyW), cw),
                                     std::max(lw, T.minWidth - 2 * f - pad[0] - pad[2])));
  const double x0 = f + pad[0];
  double y = f + pad[1];
  const double cy = y + rowT / 2;
  if (mark < 0) {
    text(part("title", x0, y, tw + 2, rowT), title);
  } else {
    const std::string pre = title.substr(0, mark), ch = title.substr(mark, 1);
    const double pw = Measure(Dt, pre, px), chw = Measure(Dt, ch, px);
    text(part("title", x0, y, pw + 2, rowT), pre);
    TipPart &m = part("mark", x0 + pw, y, chw + 2, rowT);
    text(m, ch);
    m.bColour = true;
    memcpy(m.colour, T.mark, sizeof m.colour);
    text(part("title2", x0 + pw + chw, y, std::max(2.0, tw - pw - chw + 2), rowT), title.substr(mark + 1));
    if (T.markLine > 0)
      part("markLine", x0 + pw, cy + Dt.size * 0.46, std::max(1.0, chw - Dt.tracking), T.markLine);
  }
  if (!hot.empty()) {
    part("key", x0 + inner - keyW, cy - keyH / 2, keyW, keyH);
    text(part("keyText", x0 + inner - keyW, cy - keyH / 2, keyW, keyH), hot);
  }
  y += rowT;
  if (!costs.empty() || !lines.empty()) {
    y += T.gap;
    if (T.bRule) {
      part("rule", x0, y, inner, 1);
      y += 1 + T.gap;
    }
  }
  static const char *const kCap[HUD_TIP_COSTS] = {"Minerals", "Gas", "Supply", "Energy"};
  if (!costs.empty()) {
    double x = x0;
    const double ccy = y + costH / 2;
    for (int k : costs) {
      const std::string txt = TipClean(c.costs[k]);
      const double fw = Measure(Dc, txt, px);
      // the style's icon, else the original's of the cost's race (port_hudfile.cpp, TipPanel)
      std::string icon = std::string("icon") + kCap[k];
      if (index(icon) < 0)
        icon += "ZTP"[c.anRace[k] >= 0 && c.anRace[k] < 3 ? c.anRace[k] : 1];
      part(icon, x, ccy - T.icon / 2, T.icon, T.icon);
      TipPart &q = part(std::string("cost") + kCap[k], x + T.icon + T.iconGap, y, fw + 2, costH);
      text(q, txt);
      if (c.abShort[k]) {
        q.bColour = true;
        memcpy(q.colour, T.shortOf, sizeof q.colour);
      }
      x += T.icon + T.iconGap + fw + T.costGap;
    }
    y += costH;
    if (!lines.empty())
      y += T.gap;
  }
  for (size_t i = 0; i < wrapped.size(); ++i) {
    const double h = wrapped[i].size() * Dl.size * Dl.lineHeight;
    text(part("line" + std::to_string(i + 1), x0, y, inner + 1, h), lines[i]);
    y += h + (i + 1 < wrapped.size() ? floor(T.gap / 2 + 0.5) : 0);
  }
  y += pad[3] + f;
  const double bw = inner + pad[0] + pad[2] + 2 * f, bh = ceil(y);
  part("pane", f, f, bw - 2 * f, bh - 2 * f);
  if (T.nStrip == 0)
    part("strip", f + T.stripInset, f + 1.5, std::max(0.0, bw - 2 * f - 2 * T.stripInset), T.stripSize);
  else if (T.nStrip == 1)
    part("strip", f + 1.5, f + T.stripInset, T.stripSize, std::max(0.0, bh - 2 * f - 2 * T.stripInset));

  // the side: the button's panel's "tip", or by its anchor: above a bottom panel, below a top one,
  // beside a column toward the middle. Hyperion's: it stands off the button itself, as the
  // original's and Remastered's do, where the design center's stands off the button's panel
  const HudPanelDef *pOwner = m_nTipOwner >= 0 && m_nTipOwner < static_cast<int>(m_pDesign->panels.size())
                                  ? &m_pDesign->panels[m_nTipOwner]
                                  : nullptr;
  enum { ABOVE = 1, BELOW, LEFT, RIGHT, NONE };
  int side = pOwner != nullptr ? pOwner->nTip : 0;
  if (side == 0)
    side = pOwner == nullptr || pOwner->nAy == 2 ? ABOVE
           : pOwner->nAy == 0                    ? BELOW
           : pOwner->nAx == 0                    ? RIGHT
           : pOwner->nAx == 2                    ? LEFT
                                                 : ABOVE;
  if (side == NONE) {
    m_bTipShown = false;
    return false;
  }
  const double *a = m_tipAt;
  const double *pr = a;
  const double d = T.tab > 0 ? T.tab : 0, mg = T.margin, g = T.offset;
  auto cl = [](double v, double lo, double hi) { return hi < lo ? (lo + hi) / 2 : std::max(lo, std::min(hi, v)); };
  if (side == ABOVE && pr[1] - g - d - bh < mg)
    side = BELOW;
  else if (side == BELOW && pr[1] + pr[3] + g + d + bh > SCREEN - mg)
    side = ABOVE;
  else if (side == RIGHT && pr[0] + pr[2] + g + d + bw > m_lw - mg)
    side = LEFT;
  else if (side == LEFT && pr[0] - g - d - bw < mg)
    side = RIGHT;
  double left, top, ow, oh, ox = 0, oy = 0, tabAt;
  if (side == ABOVE || side == BELOW) {
    left = cl(a[0] + a[2] / 2 - bw / 2, mg, m_lw - mg - bw);
    ow = bw;
    oh = bh + d;
    tabAt = a[0] + a[2] / 2 - left;
    if (side == ABOVE) {
      top = pr[1] - g - d - bh;
    } else {
      top = pr[1] + pr[3] + g;
      oy = d;
    }
  } else {
    top = cl(a[1] + a[3] / 2 - bh / 2, mg, SCREEN - mg - bh);
    oh = bh;
    ow = bw + d;
    tabAt = a[1] + a[3] / 2 - top;
    if (side == RIGHT) {
      left = pr[0] + pr[2] + g;
      ox = d;
    } else {
      left = pr[0] - g - d - bw;
    }
  }
  m_rects[p] = {left, top, ow, oh};

  // the plate's outline: the body with its corners cut, rounded or notched, and the tab on the
  // side toward the button, its sides at 45 degrees
  {
    const double cr = std::max(0.0, std::min(std::min(T.corner, bw / 2 - 1), bh / 2 - 1));
    const double X0 = ox, Y0 = oy, X1 = X0 + bw, Y1 = Y0 + bh, half = T.tabWidth / 2, dd = std::min(d, half);
    const double len = side == LEFT || side == RIGHT ? bh : bw, lo = cr + half + 2, hi = len - cr - half - 2;
    const double t = hi < lo ? len / 2 : std::max(lo, std::min(hi, tabAt));
    std::string out;
    std::string px0, py0;
    auto to = [&](double x, double yy) {
      const std::string sx = PathNum(x), sy = PathNum(yy);
      if (sx == px0 && sy == py0)
        return;
      px0 = sx, py0 = sy;
      out += (out.empty() ? "M " : " L ") + sx + " " + sy;
    };
    auto corner = [&](double x, double yy) {
      if (cr <= 0 || T.nCorner == 1) {
        to(x, yy);
        return;
      }
      px0 = PathNum(x), py0 = PathNum(yy);
      out += " A " + PathNum(cr) + " " + PathNum(cr) + " 0 0 " + (T.nCorner == 2 ? "0 " : "1 ") + px0 + " " + py0;
    };
    to(X0 + cr, Y0);
    if (side == BELOW && d > 0) {
      to(X0 + t - half, Y0);
      to(X0 + t - half + dd, Y0 - d);
      to(X0 + t + half - dd, Y0 - d);
      to(X0 + t + half, Y0);
    }
    to(X1 - cr, Y0);
    corner(X1, Y0 + cr);
    if (side == LEFT && d > 0) {
      to(X1, Y0 + t - half);
      to(X1 + d, Y0 + t - half + dd);
      to(X1 + d, Y0 + t + half - dd);
      to(X1, Y0 + t + half);
    }
    to(X1, Y1 - cr);
    corner(X1 - cr, Y1);
    if (side == ABOVE && d > 0) {
      to(X0 + t + half, Y1);
      to(X0 + t + half - dd, Y1 + d);
      to(X0 + t - half + dd, Y1 + d);
      to(X0 + t - half, Y1);
    }
    to(X0 + cr, Y1);
    corner(X0, Y1 - cr);
    if (side == RIGHT && d > 0) {
      to(X0, Y0 + t + half);
      to(X0 - d, Y0 + t + half - dd);
      to(X0 - d, Y0 + t - half + dd);
      to(X0, Y0 + t - half);
    }
    to(X0, Y0 + cr);
    corner(X0 + cr, Y0);
    m_tipPath = out + " Z";
  }

  // each part at its place in the panel, which holds the body and the tab
  for (size_t l = 0; l < pd.layers.size(); ++l) {
    auto it = parts.find(pd.layers[l].id);
    m_tipHas[l] = it != parts.end() || static_cast<int>(l) == m_nTipPlate;
    if (static_cast<int>(l) == m_nTipPlate) {
      m_tipParts[l] = TipPart();
      m_tipParts[l].b[2] = ow;
      m_tipParts[l].b[3] = oh;
      continue;
    }
    if (it == parts.end())
      continue;
    m_tipParts[l] = it->second;
    m_tipParts[l].b[0] += ox;
    m_tipParts[l].b[1] += oy;
  }
  return true;
}

void HudPlayer::MusicPlates(const HudMusicPlate *pPlates, int nPlates) {
  for (std::vector<std::vector<MusicBox>> &w : m_musicPlates)
    for (std::vector<MusicBox> &v : w)
      v.clear();
  if (m_nMusicPanel < 0)
    return;
  // the panel round them all, each plate in it
  double x0 = 1e9, y0 = 1e9, x1 = -1e9, y1 = -1e9;
  for (int i = 0; i < nPlates; ++i) {
    x0 = std::min(x0, pPlates[i].box[0]);
    y0 = std::min(y0, pPlates[i].box[1]);
    x1 = std::max(x1, pPlates[i].box[0] + pPlates[i].box[2]);
    y1 = std::max(y1, pPlates[i].box[1] + pPlates[i].box[3]);
  }
  if (nPlates > 0) {
    const double r[4] = {x0, y0, std::max(1.0, x1 - x0), std::max(1.0, y1 - y0)};
    memcpy(m_musicRect, r, sizeof r);
  }
  if (m_nMusicPanel < static_cast<int>(m_rects.size()))
    m_rects[m_nMusicPanel].assign(m_musicRect, m_musicRect + 4);
  for (int i = 0; i < nPlates; ++i) {
    const HudMusicPlate &q = pPlates[i];
    const int nWin = q.nPart == HUD_MUSIC_MENU ? HUD_MUSIC_WINDOWS : q.nWin;
    if (q.nPart < 0 || q.nPart >= HUD_MUSIC_PARTS || nWin < 0 || nWin > HUD_MUSIC_WINDOWS)
      continue;
    MusicBox m;
    memcpy(m.b, q.box, sizeof m.b);
    m.b[0] -= m_musicRect[0];
    m.b[1] -= m_musicRect[1];
    m.opacity = q.opacity;
    m_musicPlates[nWin][q.nPart].push_back(m);
  }
}

void HudPlayer::MusicLayer(int nLayer, HudState *s) const {
  const MusicAt &at = m_musicLayers[nLayer];
  if (at.nPart < 0 || at.nIndex >= static_cast<int>(m_musicPlates[at.nWin][at.nPart].size())) {
    s->bVisible = false;
    return;
  }
  const MusicBox &q = m_musicPlates[at.nWin][at.nPart][at.nIndex];
  s->bVisible = true;
  HudOptNum *const ap[4] = {&s->x, &s->y, &s->w, &s->h};
  for (int i = 0; i < 4; ++i) {
    ap[i]->d = q.b[i];
    ap[i]->bSet = true;
  }
  s->opacity = q.opacity;
}

void HudPlayer::TipLayer(int nLayer, HudState *s) const {
  if (!m_tipHas[nLayer]) {
    s->bVisible = false;
    return;
  }
  const TipPart &q = m_tipParts[nLayer];
  const HudTooltip &T = m_pDesign->tooltip;
  s->bVisible = true;
  HudOptNum *const ap[4] = {&s->x, &s->y, &s->w, &s->h};
  for (int i = 0; i < 4; ++i) {
    ap[i]->d = q.b[i];
    ap[i]->bSet = true;
  }
  if (q.bText)
    s->text = q.text;
  if (q.bColour) {
    memcpy(s->color.c, q.colour, sizeof s->color.c);
    s->color.bSet = true;
  }
  if (nLayer == m_nTipPlate) {
    s->pPath = &m_tipPath;
    s->bPathSize = true;
    s->pathSize[0] = q.b[2];
    s->pathSize[1] = q.b[3];
    s->nPathFit = 1;
  } else if (nLayer == m_nTipPane) {
    for (int i = 0; i < 4; ++i) {
      s->corners[i] = std::max(0.0, T.corner - T.frame * 0.6);
      s->cornerKinds[i] = T.nCorner;
    }
  } else if (nLayer == m_nTipMarkLine) {
    s->nFill = 1;
    memcpy(s->fill[0], T.mark, sizeof s->fill[0]);
  }
}

bool HudPlayer::Tip(const HudTipContent *pContent, const double at[4], int nPanel) {
  if (m_nTipPanel < 0)
    return false;
  const std::vector<HudTimeline> &tls = m_pDesign->timelines;
  auto dropHides = [&]() {
    m_insts.erase(std::remove_if(m_insts.begin(), m_insts.end(),
                                 [&](const Inst &x) { return tls[x.nTl].nOn == HUD_EV_TIP_HIDE; }),
                  m_insts.end());
  };
  if (pContent == nullptr) {
    if (m_bTipNow) {
      m_bTipNow = false;
      double dur = 0;
      for (const HudTimeline &tl : tls)
        if (tl.nOn == HUD_EV_TIP_HIDE)
          dur = std::max(dur, tl.duration);
      Fire(HUD_EV_TIP_HIDE);
      m_tipHideAt = m_clock + dur;
    }
    return true;
  }
  const bool bShowing = m_bTipNow;
  m_tip = *pContent;
  if (at != nullptr) {
    memcpy(m_tipAt, at, sizeof m_tipAt);
    m_nTipOwner = nPanel;
  }
  m_bTipNow = true;
  if (!LayTip()) {
    m_bTipNow = false;
    return false;
  }
  m_bTipShown = true;
  if (!bShowing) {
    m_tipHideAt = -1;
    dropHides();
    Fire(HUD_EV_TIP_SHOW, m_nTipOwner >= 0 ? m_pDesign->panels[m_nTipOwner].id : std::string());
  }
  return true;
}
