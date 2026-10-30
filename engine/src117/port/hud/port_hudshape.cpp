// port_hudshape.cpp -- the shape layers' tables and geometry (port_hudshape.h), from
// hudshape.js in the design center.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include "port_hudshape.h"

namespace {

// hudshape.js's MATERIALS, as JSON so a style's look and a layer lay over them as the player
// does; "family" is the library's own and never reaches a layer.
const char kMaterials[] = R"({
"steel.brushed": {"family": "metal", "surface": "brushed", "surfaceA": "#a9b3bd", "surfaceB": "#5b646d", "gloss": 0.55, "detail": 1, "fill": "#ffffff", "bevel": 8, "bevelStrength": 0.7},
"steel.gunmetal": {"family": "metal", "surface": "gunmetal", "surfaceA": "#4d5760", "surfaceB": "#262c33", "gloss": 0.35, "detail": 0.7, "fill": "#ffffff", "bevel": 8, "bevelStrength": 0.7},
"plate.painted": {"family": "metal", "surface": "painted", "surfaceA": "#46523f", "surfaceB": "#8d969e", "gloss": 0.45, "wear": 0.45, "detail": 1, "fill": "#ffffff", "bevel": 7, "bevelStrength": 0.6},
"paint.hazard": {"family": "metal", "surface": "hazard", "surfaceA": "#e8b923", "surfaceC": "#17191b", "surfaceB": "#8d969e", "gloss": 0.4, "wear": 0.5, "detail": 1, "fill": "#ffffff", "bevel": 6, "bevelStrength": 0.6},
"metal.chrome": {"family": "metal", "surface": "chrome", "surfaceA": "#eef5fb", "surfaceB": "#1b2229", "gloss": 1, "fill": "#ffffff", "bevel": 6, "bevelStrength": 0.9},
"trim.gold": {"family": "metal", "surface": "polished", "surfaceA": "#f4d273", "surfaceB": "#8a5a17", "gloss": 0.85, "detail": 0.5, "fill": "#ffffff", "bevel": 4, "bevelStrength": 0.9},
"trim.bronze": {"family": "metal", "surface": "polished", "surfaceA": "#d69c60", "surfaceB": "#5e3517", "gloss": 0.75, "detail": 0.5, "fill": "#ffffff", "bevel": 4, "bevelStrength": 0.9},
"composite.carbon": {"family": "composite", "surface": "carbon", "surfaceA": "#40464d", "surfaceB": "#0f1113", "gloss": 0.6, "detail": 1, "fill": "#ffffff", "bevel": 6, "bevelStrength": 0.6},
"composite.polymer": {"family": "composite", "surface": "polymer", "surfaceA": "#2c3238", "gloss": 0.12, "detail": 0.6, "fill": "#ffffff", "bevel": 6, "bevelStrength": 0.5},
"glass.frosted": {"family": "glass", "fill": ["#3a4c5e60", "#18222c88", "#0a0f15b8"], "glass": 0.85, "glassTint": "#0a1824a0", "glassNoise": 0.035, "glassStreak": 0.2, "bevel": 10, "bevelStrength": 0.55},
"glass.visor": {"family": "glass", "fill": ["#ffb34a30", "#1a0e0490", "#000000c0"], "glass": 0.9, "glassTint": "#2a1606b0", "glassNoise": 0.02, "glassStreak": 0.45, "bevel": 6, "bevelStrength": 0.7},
"energy.hologram": {"family": "glass", "fill": "#2fa8ff1a", "holo": 0.22, "lines": 0.35, "lineSpacing": 3, "innerGlow": "#5fd0ff60", "innerGlowSize": 10, "stroke": "#7fdcffa0", "line": 1},
"energy.plasma": {"family": "glass", "fill": "#00000000", "energy": 1, "energyColor": "#5fd0ff", "energyWidth": 7, "outerGlow": "#2fa8ff80", "outerGlowSize": 10},
"organic.chitin": {"family": "organic", "surface": "chitin", "surfaceA": "#4c2c3b", "surfaceB": "#b07a92", "surfaceC": "#120a10", "gloss": 0.7, "detail": 1, "fill": "#ffffff", "bevel": 12, "bevelStrength": 0.7},
"organic.membrane": {"family": "organic", "surface": "membrane", "surfaceA": "#b9607a", "surfaceB": "#4f1e30", "gloss": 0.85, "detail": 1, "fill": "#ffffff", "glass": 0.35, "glassTint": "#3a0a1aa0", "bevel": 10, "bevelStrength": 0.6},
"organic.flesh": {"family": "organic", "surface": "flesh", "surfaceA": "#7a2e3a", "surfaceB": "#3a1030", "surfaceC": "#ff5a3a", "gloss": 0.45, "detail": 1, "fill": "#ffffff", "bevel": 10, "bevelStrength": 0.6},
"organic.sacs": {"family": "organic", "surface": "sacs", "surfaceA": "#3a1a2a", "surfaceB": "#5a2a3a", "surfaceC": "#ffb03a", "gloss": 0.6, "detail": 1, "fill": "#ffffff", "bevel": 8, "bevelStrength": 0.6},
"crystal.khaydarin": {"family": "crystal", "surface": "crystal", "surfaceA": "#5fd8ff", "surfaceB": "#0a3a6a", "surfaceC": "#bff4ff", "gloss": 0.95, "detail": 1, "fill": "#ffffffd0", "glass": 0.5, "glassTint": "#0a2a50a0", "bevel": 8, "bevelStrength": 0.8},
"crystal.filigree": {"family": "crystal", "surface": "filigree", "surfaceA": "#f4d273", "surfaceB": "#e7e2d6", "gloss": 0.8, "detail": 1, "fill": "#ffffff", "bevel": 8, "bevelStrength": 0.6},
"crystal.psionic": {"family": "crystal", "surface": "psionic", "surfaceA": "#2f8cff", "surfaceB": "#06122a", "surfaceC": "#9fe8ff", "gloss": 0.3, "detail": 1, "fill": "#ffffff", "bevel": 6, "bevelStrength": 0.5},
"crystal.ceramic": {"family": "crystal", "surface": "ceramic", "surfaceA": "#ebe8e1", "surfaceB": "#b3ad9f", "gloss": 0.55, "fill": "#ffffff", "bevel": 10, "bevelStrength": 0.6},
"mech.gunmetal": {"family": "mech", "surface": "machined", "surfaceA": "#3b424b", "surfaceB": "#1b1f25", "gloss": 0.5, "detail": 1, "fill": "#ffffff", "bevel": 4, "bevelStrength": 1, "profile": "chamfer", "sheen": 0.5, "sheenColor": "#a9bdd0", "edgeWear": 0.3, "edgeWearWidth": 1.2, "edgeWearColor": "#8f9aa6"},
"mech.graphite": {"family": "mech", "surface": "cerakote", "surfaceA": "#3e4044", "surfaceB": "#2a2c30", "gloss": 0.16, "detail": 1, "fill": "#ffffff", "bevel": 4, "bevelStrength": 0.9, "profile": "chamfer", "sheen": 0.25, "sheenColor": "#c8ccd2", "edgeWear": 0.4, "edgeWearWidth": 1.4, "edgeWearColor": "#9a9fa6"},
"mech.blackout": {"family": "mech", "surface": "machined", "surfaceA": "#1f2226", "surfaceB": "#0b0c0e", "gloss": 0.62, "detail": 0.8, "fill": "#ffffff", "bevel": 4, "bevelStrength": 1.1, "profile": "chamfer", "sheen": 0.6, "sheenColor": "#8e9aa8", "edgeWear": 0.15, "edgeWearWidth": 1, "edgeWearColor": "#7d8792"},
"mech.armor": {"family": "mech", "surface": "armor", "surfaceA": "#363c43", "surfaceB": "#262b31", "surfaceC": "#07090b", "surfaceScale": 1, "gloss": 0.42, "detail": 1, "fill": "#ffffff", "bevel": 4, "bevelStrength": 1, "profile": "chamfer", "sheen": 0.45, "sheenColor": "#a9bdd0", "edgeWear": 0.25, "edgeWearWidth": 1.2, "edgeWearColor": "#8f9aa6"},
"mech.forged": {"family": "mech", "surface": "forged", "surfaceA": "#363a40", "surfaceB": "#0d0e10", "gloss": 0.6, "detail": 1, "fill": "#ffffff", "bevel": 3, "bevelStrength": 0.9, "profile": "chamfer", "sheen": 0.5, "sheenColor": "#b0bccb"},
"mech.steel": {"family": "mech", "surface": "brushed", "surfaceA": "#9aa5b1", "surfaceB": "#4a535d", "gloss": 0.7, "detail": 0.8, "fill": "#ffffff", "bevel": 1.5, "bevelStrength": 1.1, "profile": "chamfer", "sheen": 0.5, "sheenColor": "#e6eef6"},
"mech.rubber": {"family": "mech", "surface": "polymer", "surfaceA": "#16181b", "gloss": 0.08, "detail": 0.5, "fill": "#ffffff", "bevel": 2, "bevelStrength": 0.6},
"glass.smoked": {"family": "glass", "fill": ["#0d1116d8", "#05070aee"], "fillAngle": 100, "glass": 1, "glassTint": "#020305c8", "glassBlur": 4, "glassNoise": 0.015, "glassStreak": 0.06, "innerShadow": 2, "innerShadowStrength": 0.85}
})";

const char *const kSurfaces[] = {"none", "brushed", "gunmetal", "painted", "hazard", "chrome", "polished",
                                 "carbon", "polymer", "chitin", "membrane", "flesh", "sacs", "crystal",
                                 "filigree", "psionic", "ceramic", "machined", "cerakote", "forged", "armor"};
const char *const kLiveSurfaces[] = {"membrane", "flesh", "sacs", "crystal", "psionic"};
const char *const kProfiles[] = {"slope", "step", "double", "lip", "groove", "chamfer"};
const char *const kPatterns[] = {"none", "honeycomb", "slats", "perforated"};
const char *const kBoltStyles[] = {"dome", "hex", "socket"};

template <size_t N>
int IndexOf(const char *const (&list)[N], const std::string &name) {
  for (size_t i = 0; i < N; ++i)
    if (name == list[i])
      return static_cast<int>(i);
  return -1;
}

template <size_t N>
std::string Join(const char *const (&list)[N]) {
  std::string s;
  for (size_t i = 0; i < N; ++i)
    s += std::string(i ? ", " : "") + list[i];
  return s;
}

const PortJson &Materials() {
  static PortJson s_materials;
  static bool s_bRead = false;
  if (!s_bRead) {
    s_bRead = true;
    PortJsonParse(kMaterials, &s_materials);
  }
  return s_materials;
}

// An SVG arc as points, the end point included (the spec's endpoint-to-centre conversion).
void Arc(double x1, double y1, double rx, double ry, double phi, double large, double sweep, double x2, double y2,
         std::vector<HudPoint> *pOut) {
  if (rx == 0.0 || ry == 0.0) {
    pOut->push_back({x2, y2});
    return;
  }
  const double c = cos(phi), s = sin(phi);
  const double dx = (x1 - x2) / 2, dy = (y1 - y2) / 2;
  const double x1p = c * dx + s * dy, y1p = -s * dx + c * dy;
  const double lam = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
  if (lam > 1) {
    rx *= sqrt(lam);
    ry *= sqrt(lam);
  }
  const double num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p;
  const double den = rx * rx * y1p * y1p + ry * ry * x1p * x1p;
  const double co = sqrt(fmax(0.0, den != 0.0 ? num / den : 0.0)) * (large == sweep ? -1 : 1);
  const double cxp = co * rx * y1p / ry, cyp = -co * ry * x1p / rx;
  const double cx = c * cxp - s * cyp + (x1 + x2) / 2, cy = s * cxp + c * cyp + (y1 + y2) / 2;
  auto ang = [](double ux, double uy, double vx, double vy) { return atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
  const double t1 = ang(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry);
  double dt = ang((x1p - cxp) / rx, (y1p - cyp) / ry, (-x1p - cxp) / rx, (-y1p - cyp) / ry);
  const double PI = 3.14159265358979323846;
  if (sweep == 0 && dt > 0)
    dt -= 2 * PI;
  else if (sweep != 0 && dt < 0)
    dt += 2 * PI;
  int n = static_cast<int>(fmax(2.0, fmin(32.0, ceil(fabs(dt) * fmax(rx, ry) / 5))));
  n = static_cast<int>(fmax(n, ceil(fabs(dt) / (PI / 8))));
  const size_t first = pOut->size();
  for (int k = 1; k <= n; ++k) {
    const double t = t1 + dt * k / n;
    pOut->push_back({cx + c * rx * cos(t) - s * ry * sin(t), cy + s * rx * cos(t) + c * ry * sin(t)});
  }
  if (pOut->size() > first)
    pOut->back() = {x2, y2};
}

bool IsCommand(const std::string &t) { return t.size() == 1 && isalpha(static_cast<unsigned char>(t[0])); }

std::vector<std::string> PathTokens(const std::string &d) {
  // /[MLHVQCAZmlhvqcaz]|-?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?/g
  std::vector<std::string> out;
  size_t i = 0;
  const size_t n = d.size();
  while (i < n) {
    const char c = d[i];
    if (strchr("MLHVQCAZmlhvqcaz", c) != NULL && c != '\0') {
      out.push_back(std::string(1, c));
      ++i;
      continue;
    }
    size_t j = i;
    if (d[j] == '-')
      ++j;
    const size_t digits = j;
    if (j < n && isdigit(static_cast<unsigned char>(d[j]))) {
      while (j < n && isdigit(static_cast<unsigned char>(d[j])))
        ++j;
      if (j < n && d[j] == '.') {
        ++j;
        while (j < n && isdigit(static_cast<unsigned char>(d[j])))
          ++j;
      }
    } else if (j + 1 < n && d[j] == '.' && isdigit(static_cast<unsigned char>(d[j + 1]))) {
      ++j;
      while (j < n && isdigit(static_cast<unsigned char>(d[j])))
        ++j;
    } else {
      ++i;
      continue;
    }
    if (j < n && (d[j] == 'e' || d[j] == 'E') && j > digits) {
      size_t k = j + 1;
      if (k < n && (d[k] == '-' || d[k] == '+'))
        ++k;
      if (k < n && isdigit(static_cast<unsigned char>(d[k]))) {
        while (k < n && isdigit(static_cast<unsigned char>(d[k])))
          ++k;
        j = k;
      }
    }
    out.push_back(d.substr(i, j - i));
    i = j;
  }
  return out;
}

}  // namespace

const PortJson *HudMaterial(const std::string &name) {
  const PortJson &m = Materials()[name.c_str()];
  return m.kind == PortJson::OBJECT ? &m : nullptr;
}

std::string HudMaterialNames(void) {
  std::string s;
  for (const PortJsonMember &m : Materials().members)
    s += (s.empty() ? "" : ", ") + m.first;
  return s;
}

int HudSurfaceOf(const std::string &name) { return IndexOf(kSurfaces, name); }
std::string HudSurfaceNames(void) { return Join(kSurfaces); }
bool HudSurfaceLive(const std::string &name) { return IndexOf(kLiveSurfaces, name) >= 0; }
int HudProfileOf(const std::string &name) { return IndexOf(kProfiles, name); }
std::string HudProfileNames(void) { return Join(kProfiles); }
int HudPatternOf(const std::string &name) { return IndexOf(kPatterns, name); }
std::string HudPatternNames(void) { return Join(kPatterns); }
int HudBoltStyleOf(const std::string &name) { return IndexOf(kBoltStyles, name); }

const std::vector<std::vector<HudPoint>> &HudFlatten(const std::string &d) {
  static std::map<std::string, std::vector<std::vector<HudPoint>>> s_cache;
  auto it = s_cache.find(d);
  if (it != s_cache.end())
    return it->second;
  // a tooltip's outline is made for each one shown: the cache is kept from growing
  if (s_cache.size() >= 1024)
    s_cache.clear();
  const std::vector<std::string> toks = PathTokens(d);
  std::vector<std::vector<HudPoint>> loops;
  int cur = -1;
  double x = 0, y = 0, sx = 0, sy = 0;
  size_t i = 0;
  char cmd = 0;
  auto num = [&]() { return i < toks.size() ? atof(toks[i++].c_str()) : 0.0; };
  auto isNum = [&]() { return i < toks.size() && !IsCommand(toks[i]); };
  auto add = [&](double px, double py) {
    if (cur < 0) {
      loops.push_back({{x, y}});
      cur = static_cast<int>(loops.size()) - 1;
    }
    loops[cur].push_back({px, py});
    x = px;
    y = py;
  };
  auto steps = [](double len) { return static_cast<int>(fmax(2.0, fmin(24.0, ceil(len / 6)))); };
  while (i < toks.size()) {
    if (IsCommand(toks[i]))
      cmd = toks[i++][0];
    else if (cmd == 0) {
      ++i;
      continue;
    }
    const bool rel = islower(static_cast<unsigned char>(cmd)) != 0;
    const char C = static_cast<char>(toupper(static_cast<unsigned char>(cmd)));
    const double ox = rel ? x : 0, oy = rel ? y : 0;
    if (C == 'Z') {
      if (cur >= 0 && loops[cur].size() > 1) {
        x = sx;
        y = sy;
      }
      cur = -1;
      cmd = 0;
      continue;
    }
    if (!isNum()) {
      cmd = 0;
      continue;
    }
    if (C == 'M') {
      x = ox + num();
      y = oy + num();
      sx = x;
      sy = y;
      loops.push_back({{x, y}});
      cur = static_cast<int>(loops.size()) - 1;
      cmd = rel ? 'l' : 'L';
    } else if (C == 'L') {
      const double px = ox + num();
      const double py = oy + num();
      add(px, py);
    } else if (C == 'H') {
      add(ox + num(), y);
    } else if (C == 'V') {
      const double py = oy + num();
      add(x, py);
    } else if (C == 'Q') {
      const double qx = ox + num(), qy = oy + num(), ex = ox + num(), ey = oy + num(), x0 = x, y0 = y;
      const int nq = steps(hypot(qx - x0, qy - y0) + hypot(ex - qx, ey - qy));
      for (int k = 1; k <= nq; ++k) {
        const double t = static_cast<double>(k) / nq, mt = 1 - t;
        add(mt * mt * x0 + 2 * mt * t * qx + t * t * ex, mt * mt * y0 + 2 * mt * t * qy + t * t * ey);
      }
    } else if (C == 'C') {
      const double c1x = ox + num(), c1y = oy + num(), c2x = ox + num(), c2y = oy + num(), cx = ox + num(),
                   cy = oy + num(), a0 = x, b0 = y;
      const int nc = steps(hypot(c1x - a0, c1y - b0) + hypot(c2x - c1x, c2y - c1y) + hypot(cx - c2x, cy - c2y));
      for (int k = 1; k <= nc; ++k) {
        const double t = static_cast<double>(k) / nc, m = 1 - t;
        add(m * m * m * a0 + 3 * m * m * t * c1x + 3 * m * t * t * c2x + t * t * t * cx,
            m * m * m * b0 + 3 * m * m * t * c1y + 3 * m * t * t * c2y + t * t * t * cy);
      }
    } else if (C == 'A') {
      const double rx = fabs(num()), ry = fabs(num()), rot = num() * 3.14159265358979323846 / 180;
      const double large = num(), sweep = num();
      const double ax = ox + num(), ay = oy + num();
      std::vector<HudPoint> pts;
      Arc(x, y, rx, ry, rot, large, sweep, ax, ay, &pts);
      for (const HudPoint &q : pts)
        add(q.x, q.y);
    } else {
      ++i;
    }
  }
  std::vector<std::vector<HudPoint>> out;
  for (auto &l : loops)
    if (l.size() > 2)
      out.push_back(l);
  return s_cache[d] = out;
}

void HudPathEdges(const std::string &path, const HudPathFit &f, double w, double h, std::vector<float> *pEdges) {
  pEdges->clear();
  const std::vector<std::vector<HudPoint>> &loops = HudFlatten(path);
  const double ps0 = f.bSize ? f.size[0] : w, ps1 = f.bSize ? f.size[1] : h;
  // mx and my map the path's units into the box's, as hudshape.js's pathEdges
  struct Band {
    int nKind;  // 0 stretch, 1 fit, 2 slice
    double k, o, lo, hi, room, a, b, S, len;
    double Map(double v) const {
      if (nKind == 0)
        return v * k;
      if (nKind == 1)
        return o + v * k;
      if (v <= a)
        return a > 0 ? v / a * lo : 0;
      if (v >= b)
        return len - (S - v) / fmax(1e-6, S - b) * hi;
      return lo + (v - a) / fmax(1e-6, b - a) * room;
    }
  };
  Band mx = {}, my = {};
  if (f.nFit == 1) {
    mx.nKind = my.nKind = 0;
    mx.k = w / ps0;
    my.k = h / ps1;
  } else if (f.nFit == 2) {
    const double kf = fmin(w / ps0, h / ps1);
    mx.nKind = my.nKind = 1;
    mx.k = my.k = kf;
    mx.o = (w - ps0 * kf) / 2;
    my.o = (h - ps1 * kf) / 2;
  } else {
    const double k = f.scale != 0.0 ? f.scale : h / ps1;
    double sl[4] = {ps0 / 2, ps1 / 2, 0, 0};
    if (f.nSlice >= 2) {
      sl[0] = f.slice[0];
      sl[1] = f.slice[1];
    }
    if (f.nSlice == 4) {
      sl[2] = f.slice[2];
      sl[3] = f.slice[3];
    }
    auto band = [k](double a, double b, double S, double len) {
      Band bd = {};
      bd.nKind = 2;
      bd.a = a;
      bd.b = b;
      bd.S = S;
      bd.len = len;
      bd.lo = a * k;
      bd.hi = (S - b) * k;
      bd.room = len - bd.lo - bd.hi;
      if (bd.room < 0) {
        const double s = len / fmax(1e-6, bd.lo + bd.hi);
        bd.lo *= s;
        bd.hi *= s;
        bd.room = 0;
      }
      return bd;
    };
    mx = band(sl[0], f.nSlice == 4 ? sl[2] : sl[0], ps0, w);
    my = band(sl[1], f.nSlice == 4 ? sl[3] : sl[1], ps1, h);
  }
  for (const auto &l : loops)
    for (size_t j = 0; j < l.size(); ++j) {
      const HudPoint &a = l[j], &b = l[(j + 1) % l.size()];
      if (a.x == b.x && a.y == b.y)
        continue;
      pEdges->push_back(static_cast<float>(mx.Map(a.x) - w / 2));
      pEdges->push_back(static_cast<float>(my.Map(a.y) - h / 2));
      pEdges->push_back(static_cast<float>(mx.Map(b.x) - w / 2));
      pEdges->push_back(static_cast<float>(my.Map(b.y) - h / 2));
    }
}
