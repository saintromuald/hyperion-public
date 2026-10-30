// port_hudfile.cpp -- a HUD file read, checked and made ready to play (port_hudfile.h).
//
// The checks and the merges work on the JSON trees as the design center's player does
// (hudplayer.js: validateStyle, relight, choose, wear, validate), each written to say the same
// thing in the same words, so a note seen in the design center is the note a player sees here.
// Then the worn layout is compiled: each panel's and layer's fields with their bindings
// compiled, each timeline's keys read into numbers and colours.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <map>
#include <set>
#include "port_hudfile.h"
#include "port_hudshape.h"

namespace {

const double SCREEN = 1080;

const char *const kKeyNames[] = {
#define HUD_KEY_NAME(n) #n,
    HUD_KEYS(HUD_KEY_NAME)
#undef HUD_KEY_NAME
};

const char *const kSlotNames[HUD_SLOTS] = {"minimap", "selection", "portrait", "card",  "resources", "time",
                                           "apm",     "groups",    "idle",     "chat",  "alerts",    "army",
                                           "menu",    "diplomacy", "messaging", "terrain", "play",     "slower",
                                           "faster",  "tv",        "follow",   "players", "stats"};
// which slots 1.17 has (hudplayer.js's SLOT_ORIGIN)
const bool kSlotClassic[HUD_SLOTS] = {true, true, true, true, true, false, false, false, false, true,  true,
                                      false, true, true, true, true, true, true,  true,  false, false, false,
                                      false};
const char *const kEventNames[HUD_EVENTS] = {
    "game.start",    "game.victory",   "game.defeat",   "select.change", "alert.attack",       "alert.minerals",
    "alert.gas",     "alert.supply",   "research.done", "unit.done",     "panel.hover",        "panel.leave",
    "panel.press",   "console.in",     "console.out",   "selection.page", "selection.subgroup", "group.recall",
    "group.set",     "tip.show",       "tip.hide",      "replay.pause",  "replay.resume",      "replay.speed",
    "replay.vision", "stats.show",    "stats.hide",    "replay.seek"};
// what a layout's "start" sets, and the stats popup's pages
const char *const kStartSwitches[] = {"stats.side", "stats.popup", "replay.tv", "replay.follow", "minimap.terrain"};
const char *const kStatsPages[] = {"overview", "income", "army", "lost"};
const char *const kEasings[] = {"linear", "in", "out", "inOut", "back", "step", "spring"};
// the game's faces, and Hyperion's own: Chakra Petch the tooltip's, Saira and Michroma
const char *const kFonts[] = {"eurostile", "eurostile-ext", "blizzard", "chakra-petch", "saira", "michroma"};

// hudplayer.js's lists of what a panel and a layer may have (a panel's are the first 24 of
// HUD_KEYS), and what a track may move
const char *const kPanelKeys[] = {"id", "kind", "ax", "ay", "x", "y", "w", "h", "visible", "clamp", "origin", "hover",
                                  "layers", "opacity", "scale", "scaleX", "scaleY", "rotate", "shake", "flicker",
                                  "note", "between", "overlap", "z", "tip", "outside"};
// a panel's keys that a layer does not have (LAYER_KEYS is the rest of HUD_KEYS)
const char *const kPanelOnlyKeys[] = {"kind", "ax", "ay", "clamp", "hover", "layers", "between", "overlap", "z", "tip",
                                      "outside"};
const char *const kPanelAnim[] ={"x", "y", "opacity", "scale", "scaleX", "scaleY", "rotate", "shake", "flicker"};
const char *const kSlotAnim[] = {"x", "y", "opacity", "scale", "scaleX", "scaleY", "rotate", "wipe", "flicker", "shake"};
const char *const kLayerAnim[] = {
    "x", "y", "opacity", "scale", "scaleX", "scaleY", "rotate", "tint", "add", "addColor", "glow", "glowColor",
    "glowRadius", "wipe", "scan", "scanStrength", "flicker", "shake", "meter", "fill", "stroke", "color", "size",
    "line", "radius", "outline", "outlineWidth", "tracking", "corners", "glass", "glassTint", "glassNoise",
    "glassStreak", "bevel", "bevelStrength", "light", "highlight", "innerShadow", "innerShadowStrength", "rim",
    "rimWidth", "rimColor", "innerGlow", "innerGlowSize", "outerGlow", "outerGlowSize", "holo", "lines", "energy",
    "energyColor", "energyWidth", "surfaceA", "surfaceB", "surfaceC", "gloss", "wear", "detail", "wobble", "spines",
    "bow", "smooth", "segmentGap", "inlay", "inlayGlow", "inlayColor", "boltColor", "reveal", "revealColor", "lit",
    "lightElevation", "lightStrength", "ambient", "lightColor", "specular", "roughness", "reflect", "emissive",
    "grime", "contrast", "saturation", "grade", "exposure", "lampColor", "lampGlow", "lampGlowStrength", "lampPulse",
    "lampRate", "lampFlicker", "glassLines", "power", "pathScale", "profileAt", "patternDepth", "scratches",
    "castShadow", "elevation", "shadowSoft", "relief", "crest", "edgeWear", "edgeWearColor", "sheen", "sheenColor"};
const char *const kRegionKeys[] = {"id", "path", "rect", "brush", "pick", "feather", "invert", "note", "light",
                                   "lightElevation", "lightStrength", "ambient", "lightColor", "height", "bevel",
                                   "parallax", "specular", "roughness", "reflect", "emissive", "grime", "grimeScale",
                                   "contrast", "saturation", "grade", "lampPulse", "lampRate", "lampFlicker",
                                   "glassLines", "scan", "shimmer"};
const char *const kStyleKeys[] = {"kind", "version", "id", "name", "race", "note", "palette", "easings", "sounds",
                                  "assets", "shape", "looks", "timelines", "layers", "lights", "parts", "tooltip",
                                  "musicPlayer", "slots"};
const char *const kStyleTlKeys[] = {"on", "panel", "end", "stops", "tracks", "cues", "drop", "note"};
// the tooltip (hudplayer.js's TIP_KEYS, TIP_NUMS, TIP_COSTS, TIP_SIDES)
const char *const kTipKeys[] = {"pad", "frame", "gap", "minWidth", "maxWidth", "corner", "cornerStyle", "tab",
                                "tabWidth", "strip", "stripSize", "stripInset", "rule", "keyPad", "keyGap", "icon",
                                "iconGap", "costGap", "mark", "markLine", "short", "icons", "offset", "margin", "note"};
const char *const kTipNums[] = {"frame", "gap", "minWidth", "maxWidth", "corner", "tab", "tabWidth", "stripSize",
                                "stripInset", "keyGap", "icon", "iconGap", "costGap", "markLine", "offset", "margin"};
const char *const kTipCosts[] = {"minerals", "gas", "supply", "energy"};
const char *const kTipSides[] = {"auto", "above", "below", "left", "right", "none"};
// The cost icons when the style names no image of its own (Hyperion's; the design center's
// page draws shapes of its own): the original's, game\icons.grp's frames, minerals once and the
// others a frame for each race, Zerg, Terran and Protoss, which the tooltip picks among as 1.17's
// does (sgbTooltipIcon_g): 0 minerals, 1 to 3 gas, 4 to 6 supply, 7 to 9 energy.
const char *const kTipRaces[] = {"Z", "T", "P"};
// 2026-10-24, the music player's skin (hudplayer.js's MUSIC_KEYS, MUSIC_COLORS, MUSIC_PARTS,
// MUSIC_FONTS, MUSIC_SOUNDS)
const char *const kMusicKeys[] = {"version", "note", "colors", "materials", "fonts", "sounds"};
const char *const kMusicColors[] = {
    "faceTop", "faceBottom", "rim", "rimOff", "rimGlow", "bevel", "titleGlow", "titleInk", "titleInkOff", "titleRule",
    "mark", "glow", "lcd", "lcdRim", "lcdGlow", "lcdEdge", "well", "segment", "segmentOff", "segmentGlow", "led",
    "ledOff", "ink", "inkHi", "inkDim", "inkOff", "rule", "buttonTop", "buttonBottom", "buttonRim", "buttonRimHi",
    "buttonLip", "buttonLipHi", "buttonInk", "transportInk", "accent", "track", "trackGlow", "fill", "fillGlow", "knob",
    "knobRim", "knobHi", "knobRimHi", "knobGlow", "visLow", "visHigh", "visPeak", "select", "selectLine", "current",
    "hover", "scroll", "art", "menu", "menuRim", "menuInk", "note", "noteRim"};
const char *const kMusicParts[] = {"frame", "title", "button", "lit", "menu", "shade"};
const char *const kMusicFonts[] = {"lcd", "text", "title"};
const char *const kMusicSounds[] = {"open", "close", "shade", "denied"};
int TipIconFrame(int nCost, int nRace) { return nCost == 0 ? 0 : 1 + 3 * (nCost - 1) + nRace; }
// the looks the parts wear where the style gives none: a plain dark plate, Chakra Petch
const char *const kTipLooks = R"({
  "tip": {"fill": ["#1c232b", "#0f1419"], "bevel": 1.6, "bevelStrength": 0.8, "rim": 0.3, "rimWidth": 0.4,
          "rimColor": "#c4d6e6", "stroke": "#030405", "line": 0.8, "outerGlow": "#000000a0", "outerGlowSize": 7},
  "tip.pane": {"fill": ["#0b1117", "#06090c"], "fillAngle": 90, "innerShadow": 1.2, "innerShadowStrength": 0.7,
               "stroke": "#2b3a48", "line": 0.6},
  "tip.strip": {"fill": "#7fdcff", "outerGlow": "#2aa8ff60", "outerGlowSize": 2},
  "tip.rule": {"fill": "#ffffff14"},
  "tip.title": {"font": "chakra-petch", "weight": 600, "size": 16, "color": "#e4eef6"},
  "tip.key": {"fill": ["#132430", "#0b151c"], "fillAngle": 90, "stroke": "#7fdcff90", "line": 0.7,
              "innerGlow": "#7fdcff26", "innerGlowSize": 3, "corners": 2, "cornerStyle": "chamfer"},
  "tip.keyText": {"font": "chakra-petch", "weight": 600, "size": 12, "color": "#c4f0ff", "tracking": 0.4},
  "tip.cost": {"font": "chakra-petch", "weight": 500, "size": 14, "color": "#f2f8ff"},
  "tip.line": {"font": "chakra-petch", "weight": 400, "size": 13, "color": "#a9b8c6", "lineHeight": 1.3}
})";
const char *const kOptionKeys[] = {"id", "label", "help", "default", "values"};
// a value's "band" is the file's band while it is picked (Width, Full screen or 16:9)
const char *const kValueKeys[] = {"id", "label", "help", "set", "band"};
const char *const kSetNever[] = {"id", "layers", "slot", "image", "rect", "text", "shape", "run", "between", "outside"};
const char *const kMultiplayerSlots[] = {"menu", "diplomacy", "messaging"};
// what a replay layout cannot do without: the Game Menu (Quit Replay) and play
const char *const kReplaySlots[] = {"menu", "play"};
const char *const kButtonSlots[] = {"menu", "diplomacy", "messaging", "terrain", "play", "slower", "faster", "tv", "follow"};

template <size_t N>
bool In(const char *const (&list)[N], const std::string &s) {
  for (size_t i = 0; i < N; ++i)
    if (s == list[i])
      return true;
  return false;
}

template <size_t N>
std::string Join(const char *const (&list)[N], const char *pszSep = ", ") {
  std::string s;
  for (size_t i = 0; i < N; ++i)
    s += std::string(i ? pszSep : "") + list[i];
  return s;
}

// ---------------------------------------------------------------------------
// The JSON trees, changed in place.

bool IsObj(const PortJson &v) { return v.kind == PortJson::OBJECT; }
bool IsArr(const PortJson &v) { return v.kind == PortJson::ARRAY; }
bool IsStr(const PortJson &v) { return v.kind == PortJson::STRING; }
bool IsNum(const PortJson &v) { return v.kind == PortJson::NUMBER && isfinite(v.d); }
bool Has(const PortJson &v) { return v.kind != PortJson::NONE && v.kind != PortJson::NULLV; }
// JavaScript's truthiness of a value as the file has it
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

PortJson *Member(PortJson &o, const std::string &name) {
  if (!IsObj(o))
    return nullptr;
  for (PortJsonMember &m : o.members)
    if (m.first == name)
      return &m.second;
  return nullptr;
}

void SetMember(PortJson &o, const std::string &name, const PortJson &v) {
  if (!IsObj(o))
    return;
  if (PortJson *p = Member(o, name)) {
    *p = v;
    return;
  }
  PortJsonMember m;
  m.first = name;
  m.second = v;
  o.members.push_back(m);
}

void RemoveMember(PortJson &o, const std::string &name) {
  o.members.erase(std::remove_if(o.members.begin(), o.members.end(),
                                 [&](const PortJsonMember &m) { return m.first == name; }),
                  o.members.end());
}

// Object.assign(dst, src)
void Assign(PortJson &dst, const PortJson &src) {
  if (!IsObj(src))
    return;
  if (!IsObj(dst)) {
    dst = PortJson();
    dst.kind = PortJson::OBJECT;
  }
  for (const PortJsonMember &m : src.members)
    SetMember(dst, m.first, m.second);
}

PortJson Obj() {
  PortJson o;
  o.kind = PortJson::OBJECT;
  return o;
}

PortJson Str(const std::string &s) {
  PortJson v;
  v.kind = PortJson::STRING;
  v.s = s;
  return v;
}

PortJson Num(double d) {
  PortJson v;
  v.kind = PortJson::NUMBER;
  v.d = d;
  return v;
}

std::string NumText(double d) {
  char sz[40];
  if (d == floor(d) && fabs(d) < 1e15)
    snprintf(sz, sizeof sz, "%.0f", d == 0.0 ? 0.0 : d);
  else
    snprintf(sz, sizeof sz, "%.15g", d);
  return sz;
}

// String(v) of a palette value, for a $name inside a longer string
std::string ValueText(const PortJson &v) {
  switch (v.kind) {
    case PortJson::STRING: return v.s;
    case PortJson::NUMBER: return NumText(v.d);
    case PortJson::BOOL: return v.b ? "true" : "false";
    case PortJson::NULLV: return "null";
    default: return PortJsonWrite(v);
  }
}

bool IsRef(const std::string &s) {
  // /^\$[A-Za-z][\w.-]*$/
  if (s.size() < 2 || s[0] != '$' || !isalpha(static_cast<unsigned char>(s[1])))
    return false;
  for (size_t i = 2; i < s.size(); ++i)
    if (!(isalnum(static_cast<unsigned char>(s[i])) || s[i] == '_' || s[i] == '.' || s[i] == '-'))
      return false;
  return true;
}

bool IsHex(const std::string &s) {
  // /^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$/
  if ((s.size() != 7 && s.size() != 9) || s[0] != '#')
    return false;
  for (size_t i = 1; i < s.size(); ++i)
    if (!isxdigit(static_cast<unsigned char>(s[i])))
      return false;
  return true;
}

// "$name": the value in table. Inside a longer string (a binding), each $name is replaced by
// the value's text.
PortJson Swap(const PortJson &v, const PortJson &table, std::set<std::string> *pMissing) {
  if (IsStr(v)) {
    if (IsRef(v.s)) {
      const PortJson &t = table[v.s.c_str() + 1];
      if (t.kind != PortJson::NONE)
        return t;
      pMissing->insert(v.s);
      return v;
    }
    if (v.s.find('$') == std::string::npos)
      return v;
    std::string out;
    const std::string &s = v.s;
    size_t i = 0;
    while (i < s.size()) {
      if (s[i] == '$' && i + 1 < s.size() && isalpha(static_cast<unsigned char>(s[i + 1]))) {
        size_t j = i + 2;
        while (j < s.size() && (isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_' || s[j] == '.' || s[j] == '-'))
          ++j;
        const std::string name = s.substr(i + 1, j - i - 1);
        const PortJson &t = table[name.c_str()];
        if (t.kind == PortJson::NONE) {
          pMissing->insert("$" + name);
          out += "$" + name;
        } else {
          out += ValueText(t);
        }
        i = j;
      } else {
        out += s[i++];
      }
    }
    return Str(out);
  }
  if (IsArr(v)) {
    PortJson a = v;
    for (PortJson &x : a.items)
      x = Swap(x, table, pMissing);
    return a;
  }
  if (IsObj(v)) {
    PortJson o = v;
    for (PortJsonMember &m : o.members)
      m.second = Swap(m.second, table, pMissing);
    return o;
  }
  return v;
}

// ---------------------------------------------------------------------------
// Colours.

struct Hsl {
  double h, s, l;
  std::string a;
};

Hsl ToHsl(const std::string &c) {
  const double r = strtol(c.substr(1, 2).c_str(), nullptr, 16) / 255.0;
  const double g = strtol(c.substr(3, 2).c_str(), nullptr, 16) / 255.0;
  const double b = strtol(c.substr(5, 2).c_str(), nullptr, 16) / 255.0;
  const double mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
  const double l = (mx + mn) / 2, d = mx - mn;
  double h = 0, s = 0;
  if (d > 0) {
    s = d / (1 - fabs(2 * l - 1));
    h = mx == r ? fmod((g - b) / d, 6.0) : mx == g ? (b - r) / d + 2 : (r - g) / d + 4;
    h = fmod(h * 60 + 360, 360.0);
  }
  return {h, std::min(1.0, s), l, c.size() > 7 ? c.substr(7, 2) : std::string()};
}

std::string FromHsl(double h, double s, double l, const std::string &a) {
  const double C = (1 - fabs(2 * l - 1)) * s, X = C * (1 - fabs(fmod(h / 60, 2.0) - 1)), m = l - C / 2;
  double rgb[3];
  if (h < 60) { rgb[0] = C; rgb[1] = X; rgb[2] = 0; }
  else if (h < 120) { rgb[0] = X; rgb[1] = C; rgb[2] = 0; }
  else if (h < 180) { rgb[0] = 0; rgb[1] = C; rgb[2] = X; }
  else if (h < 240) { rgb[0] = 0; rgb[1] = X; rgb[2] = C; }
  else if (h < 300) { rgb[0] = X; rgb[1] = 0; rgb[2] = C; }
  else { rgb[0] = C; rgb[1] = 0; rgb[2] = X; }
  char sz[16];
  snprintf(sz, sizeof sz, "#%02x%02x%02x", static_cast<int>(floor(std::max(0.0, std::min(1.0, rgb[0] + m)) * 255 + 0.5)),
           static_cast<int>(floor(std::max(0.0, std::min(1.0, rgb[1] + m)) * 255 + 0.5)),
           static_cast<int>(floor(std::max(0.0, std::min(1.0, rgb[2] + m)) * 255 + 0.5)));
  return std::string(sz) + a;
}

bool IsRgb6(const std::string &s) { return s.size() == 7 && IsHex(s); }

// A player's light colour (Settings): the style with each palette name its "lights" lists in
// the colour picked. The first, the lamp, becomes that colour; each other keeps its place
// beside the lamp, lighter or darker, more or less saturated, and its own alpha.
void Relight(PortJson &style, const std::string &color) {
  const PortJson &lights = style["lights"];
  if (!IsArr(lights) || lights.items.empty() || !IsRgb6(color))
    return;
  PortJson *pPal = Member(style, "palette");
  if (pPal == nullptr)
    return;
  const PortJson pal = *pPal;
  const PortJson &base = pal[lights.items[0].Str().c_str()];
  if (!IsStr(base) || !IsHex(base.s))
    return;
  const Hsl to = ToHsl(color), from = ToHsl(base.s);
  std::string lower = color;
  for (char &ch : lower)
    ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
  for (size_t i = 0; i < lights.items.size(); ++i) {
    const std::string k = lights.items[i].Str();
    const PortJson &pk = pal[k.c_str()];
    if (!IsStr(pk) || !IsHex(pk.s))
      continue;
    const Hsl c = ToHsl(pk.s);
    const double l = c.l <= from.l ? (from.l > 0 ? c.l * to.l / from.l : to.l)
                                   : to.l + (c.l - from.l) * (1 - to.l) / std::max(1e-6, 1 - from.l);
    const double s = from.s > 0 ? std::min(1.0, c.s * to.s / from.s) : to.s;
    SetMember(*pPal, k, Str(i == 0 ? lower + c.a : FromHsl(to.h, s, l, c.a)));
  }
}

// ---------------------------------------------------------------------------
// The checks and merges, as hudplayer.js's.

struct Notes {
  std::vector<std::string> *pErrors;
  std::vector<std::string> *pWarnings;
  void Error(const std::string &s) { pErrors->push_back(s); }
  void Warn(const std::string &s) { pWarnings->push_back(s); }
};

std::string Q(const std::string &s) { return "\"" + s + "\""; }

void CheckLook(Notes &n, const std::string &where, const PortJson &look, const std::vector<std::string> &extra) {
  if (!IsObj(look)) {
    n.Error(where + " is not an object.");
    return;
  }
  const PortJson &mat = look["material"];
  if (Has(mat) && (!IsStr(mat) || HudMaterial(mat.s) == nullptr))
    n.Error(where + ": no material " + Q(mat.Str()) + ". Materials: " + HudMaterialNames() + ".");
  const PortJson &surf = look["surface"];
  if (Has(surf) && HudSurfaceOf(surf.Str()) < 0)
    n.Error(where + ": no surface " + Q(surf.Str()) + ".");
  const PortJson &cs = look["cornerStyle"];
  auto checkCorner = [&](const PortJson &c) {
    if (c.Str() != "round" && c.Str() != "chamfer" && c.Str() != "notch")
      n.Error(where + ": \"cornerStyle\" is round, chamfer or notch.");
  };
  if (IsArr(cs))
    for (const PortJson &c : cs.items) checkCorner(c);
  else if (Has(cs))
    checkCorner(cs);
  static const char *const kPlaces[] = {"x", "y", "w", "h", "inset", "id", "text", "slot", "image", "rect", "shape", "look"};
  for (const PortJsonMember &m : look.members) {
    if (HudKeyOf(m.first) < 0 && std::find(extra.begin(), extra.end(), m.first) == extra.end())
      n.Warn(where + ": " + Q(m.first) + " means nothing and is ignored.");
    if (In(kPlaces, m.first))
      n.Error(where + ": a style does not place or name things; " + Q(m.first) + " belongs to the layout.");
  }
}

void ValidateStyle(Notes &n, const PortJson &st) {
  if (!IsObj(st)) {
    n.Error("The style is not a JSON object.");
    return;
  }
  if (st["kind"].Str() != "hud-style")
    n.Error("A style file says \"kind\": \"hud-style\".");
  if (!(IsNum(st["version"]) && st["version"].d == 1))
    n.Error("A style's \"version\" is 1.");
  for (const PortJsonMember &m : st.members)
    if (!In(kStyleKeys, m.first))
      n.Warn("Style: " + Q(m.first) + " means nothing and is ignored.");
  const PortJson &race = st["race"];
  if (Has(race) && race.Str() != "terran" && race.Str() != "zerg" && race.Str() != "protoss" && race.Str() != "any")
    n.Error("Style: \"race\" is terran, zerg, protoss or any.");
  const PortJson &lights = st["lights"];
  if (Has(lights)) {
    if (!IsArr(lights) || lights.items.empty())
      n.Error("Style: \"lights\" is a list of palette names, the lamp first.");
    else
      for (const PortJson &k : lights.items) {
        const PortJson &c = st["palette"][k.Str().c_str()];
        if (!IsStr(c) || !IsHex(c.s))
          n.Error("Style \"lights\": " + Q(k.Str()) + " is not a colour in the palette.");
      }
  }
  const PortJson &assets = st["assets"];
  for (const PortJsonMember &m : assets.members) {
    const PortJson &a = m.second;
    if (!IsObj(a) || (!IsStr(a["file"]) && !IsStr(a["audio"]) && !IsStr(a["art"])))
      n.Error("Style asset " + Q(m.first) + " needs \"audio\", \"file\" or \"art\".");
  }
  for (const PortJsonMember &m : st["easings"].members)
    if (HudEaseOf(m.second.Str()) < 0)
      n.Error("Style easing \"$" + m.first + "\": no easing " + Q(m.second.Str()) + ". Easings: " + Join(kEasings) + ".");
  for (const PortJsonMember &m : st["sounds"].members) {
    const PortJson &a = m.second;
    if (a.kind != PortJson::NULLV && !(IsStr(a) && IsStr(assets[a.s.c_str()]["audio"])))
      n.Error("Style sound \"$" + m.first + "\" names no audio asset (" + Q(a.Str()) + "); null means silent.");
  }
  if (Has(st["shape"])) {
    CheckLook(n, "Style \"shape\"", st["shape"], {"cornerScale"});
    if (Has(st["shape"]["cornerScale"]) && !IsNum(st["shape"]["cornerScale"]))
      n.Error("Style \"shape\": \"cornerScale\" is a number.");
  }
  for (const PortJsonMember &m : st["looks"].members)
    CheckLook(n, "Style look " + Q(m.first), m.second, {});
  for (const PortJsonMember &m : st["layers"].members) {
    const size_t dot = m.first.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= m.first.size() || m.first.find('.', dot + 1) != std::string::npos)
      n.Error("Style \"layers\": " + Q(m.first) + " is not a \"panel.layer\".");
    CheckLook(n, "Style layer " + Q(m.first), m.second, {});
  }
  for (const PortJsonMember &m : st["timelines"].members) {
    const std::string where = "Style timeline " + Q(m.first);
    const PortJson &t = m.second;
    if (!IsObj(t)) {
      n.Error(where + " is not an object.");
      continue;
    }
    for (const PortJsonMember &k : t.members)
      if (!In(kStyleTlKeys, k.first))
        n.Warn(where + ": " + Q(k.first) + " means nothing and is ignored.");
    if (Has(t["tracks"]) && !IsArr(t["tracks"]))
      n.Error(where + ": \"tracks\" is a list.");
    if (Has(t["drop"])) {
      bool bOk = IsArr(t["drop"]);
      for (const PortJson &d : t["drop"].items)
        bOk = bOk && IsStr(d) && d.s.find('.') != std::string::npos && d.s.find('.') > 0;
      if (!bOk)
        n.Error(where + ": \"drop\" lists \"target.prop\" names.");
    }
  }
  const PortJson &T = st["tooltip"];
  if (Has(T)) {
    const std::string tw = "Style \"tooltip\"";
    auto nums = [](const PortJson &v, size_t n) {
      bool bOk = IsArr(v) && v.size() == n;
      for (const PortJson &x : v.items)
        bOk = bOk && IsNum(x) && x.d >= 0;
      return bOk;
    };
    if (!IsObj(T)) {
      n.Error(tw + " is an object: the tooltip's sizes.");
    } else {
      for (const PortJsonMember &m : T.members)
        if (!In(kTipKeys, m.first))
          n.Warn(tw + ": " + Q(m.first) + " means nothing and is ignored.");
      for (const char *k : kTipNums)
        if (Has(T[k]) && !(IsNum(T[k]) && T[k].d >= 0))
          n.Error(tw + ": " + Q(k) + " is a number of units, 0 or more.");
      if (Has(T["pad"]) && !(nums(T["pad"], 4) || (IsNum(T["pad"]) && T["pad"].d >= 0)))
        n.Error(tw + ": \"pad\" is [left, top, right, bottom] in units, or one number for all four.");
      if (Has(T["keyPad"]) && !nums(T["keyPad"], 2))
        n.Error(tw + ": \"keyPad\" is [across, down] in units.");
      const std::string strip = T["strip"].Str();
      if (Has(T["strip"]) && strip != "top" && strip != "left" && strip != "none")
        n.Error(tw + ": \"strip\" is top, left or none.");
      const std::string cs = T["cornerStyle"].Str();
      if (Has(T["cornerStyle"]) && cs != "round" && cs != "chamfer" && cs != "notch")
        n.Error(tw + ": \"cornerStyle\" is round, chamfer or notch.");
      if (Has(T["rule"]) && T["rule"].kind != PortJson::BOOL)
        n.Error(tw + ": \"rule\" is true or false.");
      for (const char *k : {"mark", "short"})
        if (Has(T[k]) && !IsStr(T[k]))
          n.Error(tw + ": " + Q(k) + " is a colour or a palette name.");
      const PortJson &icons = T["icons"];
      if (Has(icons)) {
        if (!IsObj(icons))
          n.Error(tw + ": \"icons\" is {\"minerals\": image asset, ...}, for any of " + Join(kTipCosts) + ".");
        else
          for (const PortJsonMember &m : icons.members) {
            if (!In(kTipCosts, m.first))
              n.Warn(tw + " \"icons\": " + Q(m.first) + " is not a cost (" + Join(kTipCosts) + ").");
            else if (!IsStr(m.second))
              n.Error(tw + " \"icons\": " + Q(m.first) + " names an image asset.");
          }
      }
    }
  }
  const PortJson &MP = st["musicPlayer"];
  if (Has(MP)) {
    const std::string mw = "Style \"musicPlayer\"";
    const PortJson &mpal = st["palette"];
    const PortJson &mlooks = st["looks"];
    if (!IsObj(MP)) {
      n.Error(mw + " is an object: the music player's skin.");
    } else {
      for (const PortJsonMember &m : MP.members)
        if (!In(kMusicKeys, m.first))
          n.Warn(mw + ": " + Q(m.first) + " means nothing and is ignored.");
      if (!(IsNum(MP["version"]) && MP["version"].d == 1))
        n.Error(mw + ": \"version\" is 1.");
      for (const PortJsonMember &m : MP["colors"].members) {
        const PortJson &v = m.second;
        const PortJson &c = IsStr(v) && IsRef(v.s) ? mpal[v.s.c_str() + 1] : v;
        if (!In(kMusicColors, m.first))
          n.Warn(mw + " \"colors\": " + Q(m.first) + " is not one of the player's colours (" + Join(kMusicColors) + ").");
        else if (!(IsStr(c) && IsHex(c.s)))
          n.Error(mw + " \"colors\": " + Q(m.first) + " is a colour, #rrggbb or #rrggbbaa, or the name of one in the palette.");
      }
      for (const PortJsonMember &m : MP["materials"].members) {
        const PortJson &v = m.second;
        const std::string w2 = mw + " \"materials\": " + Q(m.first);
        if (!In(kMusicParts, m.first)) {
          n.Warn(w2 + " is not a part a look frames (" + Join(kMusicParts) + ").");
          continue;
        }
        const PortJson &name = IsObj(v) ? v["look"] : v;
        if (!IsStr(name)) {
          n.Error(w2 + " names a look, or is {\"look\": name, \"set\": {...}}.");
          continue;
        }
        if (!Has(mlooks[name.s.c_str()]))
          n.Error(w2 + ": the style has no look " + Q(name.s) + ".");
        if (IsObj(v) && Has(v["set"]))
          CheckLook(n, w2 + " \"set\"", v["set"], {});
      }
      for (const PortJsonMember &m : MP["fonts"].members) {
        const std::string f = m.second.Str();
        const PortJson &lk = mlooks[f.c_str()];
        if (!In(kMusicFonts, m.first))
          n.Warn(mw + " \"fonts\": " + Q(m.first) + " is not a role (" + Join(kMusicFonts) + ").");
        else if (!IsObj(lk) || !Truthy(lk["font"]))
          n.Error(mw + " \"fonts\": " + Q(m.first) + " names a look with a \"font\"; " + Q(f) + " is not one.");
        else if (!In(kFonts, lk["font"].Str()))
          n.Error(mw + " \"fonts\": the look " + Q(f) + " has the font " + Q(lk["font"].Str()) +
                  ", which Hyperion does not have (" + Join(kFonts) + ").");
      }
      for (const PortJsonMember &m : MP["sounds"].members) {
        const PortJson &s = m.second;
        const std::string a = IsStr(s) && IsRef(s.s) ? st["sounds"][s.s.c_str() + 1].Str() : s.Str();
        if (!In(kMusicSounds, m.first))
          n.Warn(mw + " \"sounds\": " + Q(m.first) + " is not a moment the player sounds (" + Join(kMusicSounds) + ").");
        else if (s.kind != PortJson::NULLV && !IsStr(assets[a.c_str()]["audio"]))
          n.Error(mw + " \"sounds\": " + Q(m.first) +
                  " names one of the style's sounds (\"$deploy\") or an audio asset; null is silent.");
      }
    }
  }
  PortJson looks;
  PortJsonParse(kTipLooks, &looks);
  std::string names;
  for (const PortJsonMember &m : looks.members)
    names += (names.empty() ? "" : ", ") + m.first;
  for (const PortJsonMember &m : st["looks"].members)
    if ((m.first == "tip" || m.first.compare(0, 4, "tip.") == 0) && looks[m.first.c_str()].kind == PortJson::NONE)
      n.Warn("Style look " + Q(m.first) + ": no part of the tooltip wears it (" + names + ").");
}

// The music player's skin as the style worn gives it (hudplayer.js's musicPlayerOf): each colour a
// colour, each part's look whole, each role's font and weight, each moment's audio asset.
void MusicSkinOf(const PortJson &style, HudMusicSkin *p) {
  const PortJson &MP = style["musicPlayer"];
  if (!IsObj(MP))
    return;
  const PortJson &pal = style["palette"];
  const PortJson &looks = style["looks"];
  std::set<std::string> miss;
  p->bOn = true;
  for (const PortJsonMember &m : MP["colors"].members) {
    const PortJson c = Swap(m.second, pal, &miss);
    if (In(kMusicColors, m.first) && IsStr(c) && IsHex(c.s))
      p->colors[m.first] = c.s;
  }
  PortJson base = IsObj(style["shape"]) ? style["shape"] : Obj();
  RemoveMember(base, "cornerScale");
  for (const char *k : kMusicParts) {
    const PortJson &m = MP["materials"][k];
    const std::string name = IsObj(m) ? m["look"].Str() : m.Str();
    const PortJson &look = looks[name.c_str()];
    if (name.empty() || !IsObj(look))
      continue;
    PortJson s = base;
    const PortJson *M = IsStr(look["material"]) ? HudMaterial(look["material"].s) : nullptr;
    if (M != nullptr)
      for (const PortJsonMember &mm : M->members)
        if (mm.first != "family")
          SetMember(s, mm.first, mm.second);
    Assign(s, look);
    if (IsObj(m) && IsObj(m["set"]))
      Assign(s, m["set"]);
    p->materials[k] = Swap(s, pal, &miss);
  }
  if (p->materials.count("lit") == 0 && p->materials.count("button") != 0)
    p->materials["lit"] = p->materials["button"];
  // a shaded window's bar: the title's look, its first corner all round, where a style names none
  if (p->materials.count("shade") == 0 && p->materials.count("title") != 0) {
    PortJson s = p->materials["title"];
    const PortJson &c = s["corners"];
    SetMember(s, "corners", Num(IsArr(c) && !c.items.empty() && IsNum(c.items[0]) ? c.items[0].d : IsNum(c) ? c.d : 0.0));
    p->materials["shade"] = s;
  }
  for (const char *k : kMusicFonts) {
    const PortJson &lk = looks[MP["fonts"][k].Str().c_str()];
    if (IsObj(lk) && In(kFonts, lk["font"].Str()))
      p->fonts[k] = {lk["font"].Str(), IsNum(lk["weight"]) ? lk["weight"].d : 400.0};
  }
  for (const char *k : kMusicSounds) {
    const PortJson &s = MP["sounds"][k];
    if (s.kind == PortJson::NONE)
      continue;
    p->sounds[k] = IsStr(s) && IsRef(s.s) ? style["sounds"][s.s.c_str() + 1].Str() : s.Str();
  }
}

// The picks laid over the layout: each value's "set" lays fields over panels and layers, in the
// options' order; a set never renames anything or changes what a layer is.
const PortJson *OptionValue(const PortJson &o, const std::string &pick) {
  const PortJson &vs = o["values"];
  const PortJson *pDefault = nullptr;
  const PortJson *pFirst = nullptr;
  for (const PortJson &v : vs.items) {
    if (!IsObj(v))
      continue;
    if (pFirst == nullptr)
      pFirst = &v;
    if (!pick.empty() && v["id"].Str() == pick)
      return &v;
    if (pDefault == nullptr && IsStr(o["default"]) && v["id"].Str() == o["default"].s)
      pDefault = &v;
  }
  return pDefault != nullptr ? pDefault : pFirst;
}

void Choose(PortJson &hud, const HudPicks &picks) {
  const PortJson options = hud["options"];
  if (!IsArr(options) || options.items.empty())
    return;
  PortJson *pPanels = Member(hud, "panels");
  if (pPanels == nullptr || !IsArr(*pPanels))
    return;
  // a picked value's band, set on the file once the sets are laid, which a new member would move
  const PortJson *pBand = nullptr;
  for (const PortJson &o : options.items) {
    if (!IsObj(o))
      continue;
    std::string pick;
    for (const auto &p : picks.options)
      if (p.first == o["id"].Str())
        pick = p.second;
    const PortJson *v = OptionValue(o, pick);
    if (v != nullptr && IsNum((*v)["band"]))
      pBand = &(*v)["band"];
    if (v == nullptr || !IsObj((*v)["set"]))
      continue;
    for (const PortJsonMember &t : (*v)["set"].members) {
      const size_t dot = t.first.find('.');
      const std::string pid = dot == std::string::npos ? t.first : t.first.substr(0, dot);
      PortJson *pd = nullptr;
      for (PortJson &p : pPanels->items)
        if (IsObj(p) && p["id"].Str() == pid && pd == nullptr)
          pd = &p;
      PortJson *to = pd;
      if (pd != nullptr && dot != std::string::npos) {
        to = nullptr;
        PortJson *pLayers = Member(*pd, "layers");
        if (pLayers != nullptr)
          for (PortJson &l : pLayers->items)
            if (IsObj(l) && l["id"].Str() == t.first.substr(dot + 1) && to == nullptr)
              to = &l;
      }
      if (to == nullptr || !IsObj(t.second))
        continue;
      for (const PortJsonMember &k : t.second.members)
        if (!In(kSetNever, k.first))
          SetMember(*to, k.first, k.second);
    }
  }
  if (pBand != nullptr)
    SetMember(hud, "band", *pBand);
}

// The layout with the style on. Each layer that names a look takes, from the bottom up: the
// style's "shape" (shape layers only), its material's preset, the look, then the layer's own
// values, which win; then the style's settings for that one layer. "$name" values come from
// the palette, a key's "$name" easing from the easings, a cue's "$name" sound from the sounds.
// ---------------------------------------------------------------------------
// Parts: layers and panels used by name, Hyperion's addition to the format (docs/hud-format.md,
// "Parts"). A part is {"params": {defaults}, "layers": [...]} or {"params", "panel": {...}}, in
// the layout's "parts", a parts file beside it or its style's "parts", the first found winning.
// A layer {"part": name, "with": {params}, "id", "x", "y", ...} is laid out as the part's layers:
// every value "$param" takes the param's value, "= arithmetic" of numbers and $params is worked
// out, a $param inside a longer string becomes its text, and a $name that is no param (a
// palette's colour) is left for the style. The part's layers' ids become "<use's id>-<id>",
// their x and y move by the use's, and the use's other keys are set on each. A part's layer with
// "repeat": {"count", "dx", "dy"} is laid count times, $i its index, each moved by dx, dy.

typedef std::map<std::string, const PortJson *> PartsLib;
typedef std::map<std::string, PortJson> Params;

const int kPartDepth = 8;     // parts within parts
const int kRepeatMax = 64;    // a repeat's count

// "= arithmetic": numbers, $params, + - * / and brackets. A param's name here is letters,
// digits and _ alone, so "$x-2" is $x less 2.
struct Arith {
  const char *p;
  const Params *pParams;
  bool bOk = true;
  void Skip() {
    while (*p == ' ')
      ++p;
  }
  double Factor() {
    Skip();
    if (*p == '(') {
      ++p;
      const double v = Sum();
      Skip();
      if (*p == ')')
        ++p;
      else
        bOk = false;
      return v;
    }
    if (*p == '-') {
      ++p;
      return -Factor();
    }
    if (*p == '$') {
      std::string name;
      for (++p; isalnum(static_cast<unsigned char>(*p)) || *p == '_'; ++p)
        name += *p;
      auto it = pParams->find(name);
      if (it == pParams->end() || !IsNum(it->second)) {
        bOk = false;
        return 0.0;
      }
      return it->second.d;
    }
    char *pEnd = nullptr;
    const double v = strtod(p, &pEnd);
    if (pEnd == p)
      bOk = false;
    else
      p = pEnd;
    return v;
  }
  double Product() {
    double v = Factor();
    for (;;) {
      Skip();
      if (*p == '*') {
        ++p;
        v *= Factor();
      } else if (*p == '/') {
        ++p;
        const double d = Factor();
        if (d == 0.0)
          bOk = false;
        else
          v /= d;
      } else {
        return v;
      }
    }
  }
  double Sum() {
    double v = Product();
    for (;;) {
      Skip();
      if (*p == '+') {
        ++p;
        v += Product();
      } else if (*p == '-') {
        ++p;
        v -= Product();
      } else {
        return v;
      }
    }
  }
};

// A part's value with its params put in.
bool Substitute(PortJson &v, const Params &params, std::string *pszError) {
  switch (v.kind) {
    case PortJson::STRING: {
      if (v.s.size() > 1 && v.s[0] == '$') {
        auto it = params.find(v.s.substr(1));
        if (it != params.end()) {
          v = it->second;
          return true;
        }
      }
      if (!v.s.empty() && v.s[0] == '=') {
        Arith a;
        a.p = v.s.c_str() + 1;
        a.pParams = &params;
        const double d = a.Sum();
        a.Skip();
        if (!a.bOk || *a.p != 0 || !isfinite(d)) {
          *pszError = Q(v.s) + " is not arithmetic of numbers and the part's params";
          return false;
        }
        v = Num(d);
        return true;
      }
      std::string out;
      for (size_t i = 0; i < v.s.size();) {
        if (v.s[i] != '$') {
          out += v.s[i++];
          continue;
        }
        size_t j = i + 1;
        while (j < v.s.size() && (isalnum(static_cast<unsigned char>(v.s[j])) || v.s[j] == '_' || v.s[j] == '.' || v.s[j] == '-'))
          ++j;
        auto it = params.find(v.s.substr(i + 1, j - i - 1));
        out += it != params.end() ? ValueText(it->second) : v.s.substr(i, j - i);
        i = j;
      }
      v.s = out;
      return true;
    }
    case PortJson::ARRAY:
      for (PortJson &e : v.items)
        if (!Substitute(e, params, pszError))
          return false;
      return true;
    case PortJson::OBJECT:
      for (PortJsonMember &m : v.members)
        if (!Substitute(m.second, params, pszError))
          return false;
      return true;
    default:
      return true;
  }
}

// A use's params: the part's defaults with the use's "with" over them.
Params ParamsOf(Notes &n, const std::string &where, const PortJson &part, const PortJson &use) {
  Params params;
  for (const PortJsonMember &m : part["params"].members)
    params[m.first] = m.second;
  for (const PortJsonMember &m : use["with"].members) {
    if (!params.count(m.first))
      n.Warn(where + ": the part " + Q(use["part"].Str()) + " has no param " + Q(m.first) + "; it is set all the same.");
    params[m.first] = m.second;
  }
  return params;
}

bool IsUseKey(const std::string &k) { return k == "part" || k == "with" || k == "id" || k == "x" || k == "y" || k == "note"; }

void ExpandLayers(Notes &n, const PartsLib &lib, const std::string &where, const PortJson &layers, PortJson *pOut,
                  int nDepth, std::map<std::string, int> *pUses);

// One use of a part among a panel's layers, laid out into *pOut.
void ExpandUse(Notes &n, const PartsLib &lib, const std::string &where, const PortJson &use, PortJson *pOut, int nDepth,
               std::map<std::string, int> *pUses) {
  const std::string name = use["part"].Str();
  auto it = lib.find(name);
  if (it == lib.end()) {
    n.Error(where + ": no part called " + Q(name) + ".");
    return;
  }
  if (nDepth >= kPartDepth) {
    n.Error(where + ": parts within parts go " + std::to_string(kPartDepth) + " deep at most (" + Q(name) + ").");
    return;
  }
  const PortJson &part = *it->second;
  if (!IsArr(part["layers"])) {
    n.Error(where + ": the part " + Q(name) + " has no \"layers\"; a part with a \"panel\" is used by a panel.");
    return;
  }
  const Params params = ParamsOf(n, where, part, use);
  const std::string useId = IsStr(use["id"]) && !use["id"].s.empty() ? use["id"].s : name + "-" + std::to_string(++(*pUses)[name]);
  const double dx = use["x"].Num(), dy = use["y"].Num();
  PortJson made;
  made.kind = PortJson::ARRAY;
  for (size_t k = 0; k < part["layers"].size(); ++k) {
    const PortJson &pl = part["layers"].At(k);
    PortJson rep = pl["repeat"];
    std::string err;
    const bool bRepeat = Has(rep);
    int nCount = 1;
    double rdx = 0.0, rdy = 0.0;
    if (bRepeat) {
      if (!Substitute(rep, params, &err)) {
        n.Error(where + ", part " + Q(name) + ": " + err + ".");
        return;
      }
      const double c = rep["count"].Num(-1);
      if (!IsObj(rep) || c < 0 || c > kRepeatMax || c != floor(c)) {
        n.Error(where + ", part " + Q(name) + ": \"repeat\" is {\"count\": 0 to " + std::to_string(kRepeatMax) +
                ", \"dx\", \"dy\"}.");
        return;
      }
      nCount = static_cast<int>(c);
      rdx = rep["dx"].Num();
      rdy = rep["dy"].Num();
    }
    for (int i = 0; i < nCount; ++i) {
      Params p = params;
      if (bRepeat)
        p["i"] = Num(i);
      PortJson l = pl;
      RemoveMember(l, "repeat");
      if (!Substitute(l, p, &err)) {
        n.Error(where + ", part " + Q(name) + ": " + err + ".");
        return;
      }
      if (IsNum(l["x"]))
        SetMember(l, "x", Num(l["x"].d + dx + i * rdx));
      if (IsNum(l["y"]))
        SetMember(l, "y", Num(l["y"].d + dy + i * rdy));
      const std::string lid = IsStr(l["id"]) && !l["id"].s.empty() ? l["id"].s : std::to_string(k);
      SetMember(l, "id", Str(useId + "-" + lid + (bRepeat ? "-" + std::to_string(i) : std::string())));
      for (const PortJsonMember &m : use.members)
        if (!IsUseKey(m.first))
          SetMember(l, m.first, m.second);
      made.items.push_back(l);
    }
  }
  ExpandLayers(n, lib, where, made, pOut, nDepth + 1, pUses);
}

void ExpandLayers(Notes &n, const PartsLib &lib, const std::string &where, const PortJson &layers, PortJson *pOut,
                  int nDepth, std::map<std::string, int> *pUses) {
  for (const PortJson &l : layers.items) {
    if (IsObj(l) && Has(l["part"]))
      ExpandUse(n, lib, where, l, pOut, nDepth, pUses);
    else
      pOut->items.push_back(l);
  }
}

// Every use of a part in the design laid out. pParts are the parts files beside the layout.
void ExpandParts(Notes &n, PortJson &hud, const PortJson *pStyle, const std::vector<PortJson> &files) {
  const PortJson own = hud["parts"];
  RemoveMember(hud, "parts");
  PartsLib lib;
  auto add = [&](const PortJson &parts, const std::string &from) {
    if (!Has(parts))
      return;
    if (!IsObj(parts)) {
      n.Error(from + ": \"parts\" is an object of parts by name.");
      return;
    }
    for (const PortJsonMember &m : parts.members) {
      if (!IsObj(m.second) || (!IsArr(m.second["layers"]) && !IsObj(m.second["panel"])))
        n.Error(from + ": the part " + Q(m.first) + " needs \"layers\" (a list) or \"panel\" (an object).");
      else if (Has(m.second["params"]) && !IsObj(m.second["params"]))
        n.Error(from + ": the part " + Q(m.first) + "'s \"params\" is an object of defaults.");
      else if (!lib.count(m.first))
        lib[m.first] = &m.second;
    }
  };
  add(own, "The layout");
  for (size_t i = 0; i < files.size(); ++i)
    add(files[i]["parts"], "Parts file " + std::to_string(i + 1));
  if (pStyle != nullptr)
    add((*pStyle)["parts"], "The style");
  PortJson *pPanels = Member(hud, "panels");
  if (pPanels == nullptr || !IsArr(*pPanels))
    return;
  PortJson panels;
  panels.kind = PortJson::ARRAY;
  std::map<std::string, int> uses;
  for (const PortJson &pd : pPanels->items) {
    PortJson panel = pd;
    // a panel made from a part: the part's panel, the use's keys over it, its layers after the part's
    if (IsObj(pd) && Has(pd["part"])) {
      const std::string name = pd["part"].Str(), where = "Panel " + Q(pd["id"].Str());
      auto it = lib.find(name);
      if (it == lib.end() || !IsObj((*it->second)["panel"])) {
        n.Error(where + ": no part called " + Q(name) + " with a \"panel\".");
        continue;
      }
      panel = (*it->second)["panel"];
      std::string err;
      if (!Substitute(panel, ParamsOf(n, where, *it->second, pd), &err)) {
        n.Error(where + ", part " + Q(name) + ": " + err + ".");
        continue;
      }
      for (const PortJsonMember &m : pd.members)
        if (m.first != "part" && m.first != "with" && m.first != "layers")
          SetMember(panel, m.first, m.second);
      PortJson *pLayers = Member(panel, "layers");
      if (pLayers == nullptr) {
        PortJson none;
        none.kind = PortJson::ARRAY;
        SetMember(panel, "layers", none);
        pLayers = Member(panel, "layers");
      }
      for (const PortJson &l : pd["layers"].items)
        pLayers->items.push_back(l);
    }
    if (PortJson *pLayers = Member(panel, "layers")) {
      PortJson out;
      out.kind = PortJson::ARRAY;
      ExpandLayers(n, lib, "Panel " + Q(panel["id"].Str()), *pLayers, &out, 0, &uses);
      *pLayers = out;
    }
    panels.items.push_back(panel);
  }
  *pPanels = panels;
}

// The panel a style's tooltip adds to the layout (hudplayer.js's tipPanel): every part it can
// show, each hidden until the game shows a tooltip; the player sizes and places them.
PortJson TipPanel(const PortJson &T) {
  std::string s = R"({"id": "tooltip", "kind": "tooltip", "ax": "left", "ay": "top", "x": 0, "y": 0, "w": 1, "h": 1,
    "z": 100, "visible": false, "layers": [
    {"id": "plate", "shape": true, "look": "tip"}, {"id": "pane", "shape": true, "look": "tip.pane"},
    {"id": "strip", "shape": true, "look": "tip.strip"}, {"id": "rule", "rect": true, "look": "tip.rule"},
    {"id": "title", "text": "", "look": "tip.title"}, {"id": "mark", "text": "", "look": "tip.title"},
    {"id": "title2", "text": "", "look": "tip.title"}, {"id": "markLine", "rect": true},
    {"id": "key", "shape": true, "look": "tip.key"},
    {"id": "keyText", "text": "", "look": "tip.keyText", "align": "center"})";
  for (int i = 0; i < HUD_TIP_COSTS; ++i) {
    std::string cap = kTipCosts[i];
    cap[0] = static_cast<char>(toupper(static_cast<unsigned char>(cap[0])));
    const PortJson &img = T["icons"][kTipCosts[i]];
    if (IsStr(img))
      s += ", {\"id\": \"icon" + cap + "\", \"image\": \"" + img.s + "\"}";
    else if (i == 0)
      s += ", {\"id\": \"icon" + cap + "\", \"image\": \"tip.art." + kTipCosts[i] + "\"}";
    else
      for (const char *pszRace : kTipRaces)
        s += ", {\"id\": \"icon" + cap + pszRace + "\", \"image\": \"tip.art." + kTipCosts[i] + pszRace + "\"}";
    s += ", {\"id\": \"cost" + cap + "\", \"text\": \"\", \"look\": \"tip.cost\"}";
  }
  for (int i = 1; i <= HUD_TIP_LINES; ++i)
    s += ", {\"id\": \"line" + std::to_string(i) + "\", \"text\": \"\", \"look\": \"tip.line\", \"valign\": \"top\", \"wrap\": true}";
  s += "]}";
  PortJson p;
  PortJsonParse(s.c_str(), &p);
  PortJson *pLayers = Member(p, "layers");
  for (PortJson &l : pLayers->items)
    for (const char *k : {"x", "y", "w", "h"})
      SetMember(l, k, Num(0));
  for (PortJson &l : pLayers->items) {
    PortJson f;
    f.kind = PortJson::BOOL;
    f.b = false;
    SetMember(l, "visible", f);
  }
  return p;
}

// The panel of the music player's plates a style's "musicPlayer" adds (2026-10-24): for each of
// the three windows, from the lowest, and then the menus, as many shape layers of each part a look
// frames as one shows at once ("w0-frame-0", "w0-button-13", "menu-0"), in the part's look and its
// "set", each hidden until the windows place it (HudPlayer::MusicPlates). A toggle that is on takes
// the button's look when the block gives it none; a shaded bar the title's, with the title's first
// corner all round (looks, the style's, to read it).
PortJson MusicPanel(const PortJson &MP, const PortJson &looks) {
  PortJson p;
  PortJsonParse(R"({"id": "music", "kind": "music", "ax": "left", "ay": "top", "x": 0, "y": 0, "w": 1, "h": 1,
    "z": 99, "layers": []})", &p);
  PortJson *pLayers = Member(p, "layers");
  // a window's from the bottom up: its face or its shaded bar, then its title bar, then its buttons
  static const int kOrder[HUD_MUSIC_PARTS] = {HUD_MUSIC_FRAME, HUD_MUSIC_SHADE, HUD_MUSIC_TITLE, HUD_MUSIC_BUTTON, HUD_MUSIC_LIT, HUD_MUSIC_MENU};
  for (int nWin = 0; nWin <= HUD_MUSIC_WINDOWS; ++nWin)
  for (int k : kOrder) {
    if ((k == HUD_MUSIC_MENU) != (nWin == HUD_MUSIC_WINDOWS))
      continue;
    const PortJson *pm = &MP["materials"][kMusicParts[k]];
    const bool bShadeAsTitle = k == HUD_MUSIC_SHADE && !Has(*pm);
    if (k == HUD_MUSIC_LIT && !Has(*pm))
      pm = &MP["materials"]["button"];
    if (bShadeAsTitle)
      pm = &MP["materials"]["title"];
    const PortJson &m = *pm;
    const std::string look = IsObj(m) ? m["look"].Str() : m.Str();
    if (look.empty())
      continue;
    // the title's first corner: its set's, else its look's
    double corner = 0.0;
    if (bShadeAsTitle) {
      const PortJson &sc = IsObj(m) && IsObj(m["set"]) && Has(m["set"]["corners"]) ? m["set"]["corners"] : looks[look.c_str()]["corners"];
      corner = IsArr(sc) && !sc.items.empty() && IsNum(sc.items[0]) ? sc.items[0].d : IsNum(sc) ? sc.d : 0.0;
    }
    for (int i = 0; i < kHudMusicPlates[k]; ++i) {
      PortJson l = Obj();
      if (IsObj(m) && IsObj(m["set"]))
        Assign(l, m["set"]);
      if (bShadeAsTitle)
        SetMember(l, "corners", Num(corner));
      const std::string win = nWin < HUD_MUSIC_WINDOWS ? "w" + std::to_string(nWin) + "-" : std::string();
      SetMember(l, "id", Str(win + kMusicParts[k] + "-" + std::to_string(i)));
      PortJson b;
      b.kind = PortJson::BOOL;
      b.b = true;
      SetMember(l, "shape", b);
      SetMember(l, "look", Str(look));
      for (const char *f : {"x", "y", "w", "h"})
        SetMember(l, f, Num(0));
      b.b = false;
      SetMember(l, "visible", b);
      pLayers->items.push_back(l);
    }
  }
  return p;
}

void Wear(Notes &n, PortJson &hud, const PortJson *pStyle) {
  const PortJson st = pStyle != nullptr ? *pStyle : Obj();
  const PortJson &pal = st["palette"];
  const PortJson &eas = st["easings"];
  const PortJson &snd = st["sounds"];
  std::set<std::string> miss;
  if (pStyle != nullptr) {
    PortJson assets = IsObj(hud["assets"]) ? hud["assets"] : Obj();
    Assign(assets, st["assets"]);
    SetMember(hud, "assets", assets);
  }
  // a style with a tooltip adds its panel, whose parts wear the "tip" looks (the
  // plain ones where the style gives none)
  PortJson looksAll = IsObj(st["looks"]) ? st["looks"] : Obj();
  const PortJson &T = st["tooltip"];
  if (pStyle != nullptr && IsObj(T) && IsArr(hud["panels"])) {
    bool bTaken = false;
    for (const PortJson &p : hud["panels"].items)
      bTaken = bTaken || (IsObj(p) && p["id"].Str() == "tooltip");
    if (bTaken) {
      n.Error("The layout has a panel called \"tooltip\"; with this style that name is the tooltip's.");
    } else {
      Member(hud, "panels")->items.push_back(TipPanel(T));
      PortJson merged;
      PortJsonParse(kTipLooks, &merged);
      Assign(merged, looksAll);
      looksAll = merged;
      for (const PortJsonMember &m : T["icons"].members) {
        const PortJson &a = hud["assets"][m.second.Str().c_str()];
        if (In(kTipCosts, m.first) && !(IsStr(a["file"]) || IsStr(a["art"])))
          n.Error("The style's tooltip draws " + m.first + " with " + Q(m.second.Str()) + ", which is no image asset.");
      }
      // the original's icons for the costs the style draws none of (TipPanel's tip.art.*)
      PortJson assets = IsObj(hud["assets"]) ? hud["assets"] : Obj();
      for (int i = 0; i < HUD_TIP_COSTS; ++i) {
        if (IsStr(T["icons"][kTipCosts[i]]))
          continue;
        for (int r = 0; r < (i == 0 ? 1 : 3); ++r) {
          PortJson a;
          PortJsonParse(("{\"art\": \"sd:game/icons#" + std::to_string(TipIconFrame(i, r)) +
                         "\", \"pixelated\": true, \"placeholder\": \"none\"}").c_str(),
                        &a);
          SetMember(assets, std::string("tip.art.") + kTipCosts[i] + (i == 0 ? "" : kTipRaces[r]), a);
        }
      }
      SetMember(hud, "assets", assets);
    }
    SetMember(hud, "tooltip", Swap(T, pal, &miss));
  }
  // 2026-10-24: a style whose music player has looks adds the panel of its plates
  const PortJson &MP = st["musicPlayer"];
  if (pStyle != nullptr && IsObj(MP) && IsObj(MP["materials"]) && !MP["materials"].members.empty() &&
      IsArr(hud["panels"])) {
    bool bTaken = false;
    for (const PortJson &p : hud["panels"].items)
      bTaken = bTaken || (IsObj(p) && p["id"].Str() == "music");
    if (bTaken)
      n.Error("The layout has a panel called \"music\"; with this style that name is the music player's.");
    else
      Member(hud, "panels")->items.push_back(MusicPanel(MP, st["looks"]));
  }
  PortJson shapeBase = IsObj(st["shape"]) ? st["shape"] : Obj();
  const PortJson cornerScale = shapeBase["cornerScale"];
  RemoveMember(shapeBase, "cornerScale");
  PortJson *pPanels = Member(hud, "panels");
  if (pPanels != nullptr && IsArr(*pPanels))
    for (PortJson &pd : pPanels->items) {
      if (!IsObj(pd))
        continue;
      PortJson *pLayers = Member(pd, "layers");
      if (pLayers != nullptr && IsArr(*pLayers))
        for (PortJson &l : pLayers->items) {
          if (!IsObj(l))
            continue;
          const PortJson *look = nullptr;
          if (Has(l["look"]) && IsObj(looksAll)) {
            const PortJson &lk = looksAll[l["look"].Str().c_str()];
            if (lk.kind != PortJson::NONE)
              look = &lk;
          }
          if (Has(l["look"]) && pStyle != nullptr && look == nullptr)
            n.Warn("Layer " + Q(pd["id"].Str() + "." + l["id"].Str()) + " wears the look " + Q(l["look"].Str()) +
                   ", which the style does not have; it draws as the layout has it.");
          PortJson out = Obj();
          if (Truthy(l["shape"]) && pStyle != nullptr)
            Assign(out, shapeBase);
          std::string mat = Truthy(l["material"]) ? l["material"].Str() : look != nullptr ? (*look)["material"].Str() : "";
          if (!mat.empty()) {
            const PortJson *M = HudMaterial(mat);
            if (M == nullptr)
              n.Error("Layer " + Q(pd["id"].Str() + "." + l["id"].Str()) + ": no material " + Q(mat) + ".");
            else
              for (const PortJsonMember &m : M->members)
                if (m.first != "family")
                  SetMember(out, m.first, m.second);
          }
          if (look != nullptr)
            Assign(out, *look);
          Assign(out, l);
          if (Has(l["id"]) && IsObj(st["layers"])) {
            const PortJson &own = st["layers"][(pd["id"].Str() + "." + ValueText(l["id"])).c_str()];
            if (IsObj(own))
              Assign(out, own);
          }
          if (Truthy(l["shape"]) && IsNum(cornerScale)) {
            PortJson *c = Member(out, "corners");
            if (c != nullptr) {
              if (IsArr(*c))
                for (PortJson &x : c->items) x.d *= cornerScale.d;
              else if (c->kind == PortJson::NUMBER)
                c->d *= cornerScale.d;
            }
          }
          l = Swap(out, pal, &miss);
        }
      for (PortJsonMember &m : pd.members)
        if (m.first != "layers")
          m.second = Swap(m.second, pal, &miss);
    }
  // the style's timelines change the layout's by id
  PortJson tls = IsArr(hud["timelines"]) ? hud["timelines"] : PortJson();
  if (!IsArr(tls))
    tls.kind = PortJson::ARRAY;
  const PortJson &stl = st["timelines"];
  for (PortJson &tl : tls.items) {
    if (!IsObj(tl))
      continue;
    const PortJson &o = stl[tl["id"].Str().c_str()];
    PortJson *pTracks = Member(tl, "tracks");
    if (!IsObj(o) || pTracks == nullptr || !IsArr(*pTracks))
      continue;
    std::vector<PortJson> tracks;
    for (const PortJson &tr : pTracks->items) {
      const std::string name = tr["target"].Str() + "." + tr["prop"].Str();
      bool bDrop = false;
      for (const PortJson &d : o["drop"].items)
        bDrop = bDrop || d.Str() == name;
      if (!bDrop)
        tracks.push_back(tr);
    }
    for (const PortJson &t : o["tracks"].items) {
      int j = -1;
      for (size_t i = 0; i < tracks.size(); ++i)
        if (tracks[i]["target"].Str() == t["target"].Str() && tracks[i]["prop"].Str() == t["prop"].Str())
          j = static_cast<int>(i);
      if (j >= 0)
        tracks[j] = t;
      else
        tracks.push_back(t);
    }
    pTracks->items = tracks;
    if (Truthy(o["cues"]))
      SetMember(tl, "cues", o["cues"]);
    if (Truthy(o["end"]))
      SetMember(tl, "end", o["end"]);
    if (Truthy(o["stops"]))
      SetMember(tl, "stops", o["stops"]);
  }
  for (const PortJsonMember &m : stl.members) {
    bool bHas = false;
    for (const PortJson &tl : tls.items)
      bHas = bHas || (IsObj(tl) && tl["id"].Str() == m.first);
    if (bHas)
      continue;
    if (Truthy(m.second["on"])) {
      PortJson add = m.second;
      RemoveMember(add, "drop");
      SetMember(add, "id", Str(m.first));
      if (!Has(add["tracks"])) {
        PortJson a;
        a.kind = PortJson::ARRAY;
        SetMember(add, "tracks", a);
      }
      tls.items.push_back(add);
    } else {
      n.Warn("Style timeline " + Q(m.first) + " changes a timeline this layout does not have.");
    }
  }
  for (const PortJsonMember &m : st["layers"].members) {
    const size_t dot = m.first.find('.');
    const std::string pid = m.first.substr(0, dot);
    const std::string lid = dot == std::string::npos ? std::string() : m.first.substr(dot + 1);
    bool bFound = false;
    if (pPanels != nullptr)
      for (const PortJson &pd : pPanels->items)
        if (IsObj(pd) && pd["id"].Str() == pid)
          for (const PortJson &l : pd["layers"].items)
            bFound = bFound || (IsObj(l) && ValueText(l["id"]) == lid && Has(l["id"]));
    if (!bFound)
      n.Warn("Style \"layers\": the layout has no layer " + Q(m.first) + ".");
  }
  for (PortJson &tl : tls.items) {
    if (!IsObj(tl))
      continue;
    PortJson *pTracks = Member(tl, "tracks");
    if (pTracks != nullptr)
      for (PortJson &tr : pTracks->items) {
        PortJson *pKeys = Member(tr, "keys");
        if (pKeys == nullptr || !IsArr(*pKeys))
          continue;
        for (PortJson &k : pKeys->items) {
          if (!IsArr(k))
            continue;
          if (k.items.size() > 1)
            k.items[1] = Swap(k.items[1], pal, &miss);
          if (k.items.size() > 2 && IsStr(k.items[2]) && IsRef(k.items[2].s)) {
            const PortJson &e = eas[k.items[2].s.c_str() + 1];
            if (e.kind == PortJson::NONE)
              miss.insert(k.items[2].s);
            else
              k.items[2] = e;
          }
        }
      }
    PortJson *pCues = Member(tl, "cues");
    if (pCues != nullptr && IsArr(*pCues)) {
      std::vector<PortJson> kept;
      for (PortJson c : pCues->items) {
        if (!IsArr(c) || c.items.size() < 2 || !IsStr(c.items[1]) || !IsRef(c.items[1].s)) {
          kept.push_back(c);
          continue;
        }
        const PortJson &a = snd[c.items[1].s.c_str() + 1];
        if (a.kind == PortJson::NULLV)
          continue;  // the style is silent there
        if (a.kind == PortJson::NONE) {
          if (pStyle != nullptr)
            n.Warn("Timeline " + Q(tl["id"].Str()) + ": the style has no sound " + Q(c.items[1].s) + "; that cue is silent.");
          continue;
        }
        c.items[1] = a;
        kept.push_back(c);
      }
      pCues->items = kept;
    }
  }
  SetMember(hud, "timelines", tls);
  for (const std::string &m : miss)
    n.Error(pStyle != nullptr ? "The layout names " + Q(m) + ", which the style does not give."
                              : "The layout names " + Q(m) + ": it is a layout to wear a style, and needs one.");
}

// A version 2 layout brought up to 3: each panel a framed slot.
PortJson UpgradeV2(const PortJson &obj, Notes &n) {
  static const std::map<std::string, std::vector<std::string>> kV2 = {
      {"minimap", {"minimap"}}, {"selection", {"selection"}}, {"portrait", {"portrait"}}, {"command", {"card"}},
      {"clock", {"time", "apm"}}, {"resources", {"resources"}}, {"chat", {"chat"}}, {"groups", {"groups"}},
      {"idle", {"idle"}}};
  static const std::map<std::string, std::string> kTitle = {
      {"macro", "Macro bar"}, {"formula", "Formula"}, {"apm", "Orders and army"}, {"retreat", "Auto-retreat"}};
  PortJson out = Obj();
  SetMember(out, "version", Num(3));
  SetMember(out, "screen", Num(SCREEN));
  SetMember(out, "name", Str(IsStr(obj["name"]) ? obj["name"].s : "Layout (version 2)"));
  SetMember(out, "race", Str("terran"));
  SetMember(out, "assets", Obj());
  PortJson panels;
  panels.kind = PortJson::ARRAY;
  for (const PortJson &p : obj["panels"].items) {
    if (!IsObj(p) || (p["visible"].kind == PortJson::BOOL && !p["visible"].b))
      continue;
    const double w = p["w"].Num(), h = IsNum(p["h"]) && p["h"].d != 0 ? p["h"].d : floor(w * 0.62 + 0.5);
    PortJson layers;
    layers.kind = PortJson::ARRAY;
    PortJson frame;
    PortJsonParse(R"({"id": "frame", "rect": true, "fill": "#0a0e18e6", "stroke": "#3b4755", "line": 2, "radius": 4})", &frame);
    layers.items.push_back(frame);
    const std::string kind = p["kind"].Str();
    auto it = kV2.find(kind);
    if (it != kV2.end() && it->second.size() == 2) {
      for (int i = 0; i < 2; ++i) {
        PortJson s = Obj();
        SetMember(s, "id", Str(it->second[i]));
        SetMember(s, "slot", Str(it->second[i]));
        SetMember(s, "x", Num(i == 0 ? 8 : w / 2 + 4));
        SetMember(s, "y", Num(8));
        SetMember(s, "w", Num(w / 2 - 12));
        SetMember(s, "h", Num(h - 16));
        layers.items.push_back(s);
      }
    } else if (it != kV2.end()) {
      PortJson s;
      PortJsonParse(R"({"id": "slot", "inset": [10, 10, 10, 10]})", &s);
      SetMember(s, "slot", Str(it->second[0]));
      layers.items.push_back(s);
    } else {
      n.Warn("The version 2 kind " + Q(kind) + " is an addon with no slot in version 3; it is drawn as a titled frame.");
      PortJson t;
      PortJsonParse(R"({"id": "title", "font": "eurostile", "size": 16, "color": "#7d8b98", "x": 14, "y": 8, "h": 24})", &t);
      auto ti = kTitle.find(kind);
      SetMember(t, "text", Str(ti != kTitle.end() ? ti->second : kind));
      layers.items.push_back(t);
    }
    PortJson pd = Obj();
    SetMember(pd, "id", Str(kind));
    SetMember(pd, "kind", Str(kind));
    SetMember(pd, "ax", p["ax"]);
    SetMember(pd, "ay", p["ay"]);
    SetMember(pd, "x", p["x"]);
    SetMember(pd, "y", p["y"]);
    SetMember(pd, "w", Num(w));
    SetMember(pd, "h", Num(h));
    PortJson t;
    t.kind = PortJson::BOOL;
    t.b = true;
    SetMember(pd, "clamp", t);
    SetMember(pd, "layers", layers);
    panels.items.push_back(pd);
  }
  SetMember(out, "panels", panels);
  PortJson tls;
  tls.kind = PortJson::ARRAY;
  SetMember(out, "timelines", tls);
  return out;
}

// The file checked, as hudplayer.js's validate. A binding is checked by compiling it.
void Validate(Notes &n, PortJson &obj, HudBindings &scratch) {
  if (!IsObj(obj)) {
    n.Error("The file is not a JSON object.");
    return;
  }
  const bool bV2 = (IsNum(obj["version"]) && obj["version"].d == 2) ||
                   (!Has(obj["version"]) && IsArr(obj["panels"]) && obj["panels"].size() > 0 &&
                    Truthy(obj["panels"].At(0)["kind"]) && !Has(obj["panels"].At(0)["layers"]));
  if (bV2) {
    obj = UpgradeV2(obj, n);
    n.pWarnings->insert(n.pWarnings->begin(), "A version 2 layout: its panels are shown as framed slots.");
  } else if (!(IsNum(obj["version"]) && obj["version"].d == 3)) {
    n.Error("\"version\" must be 3 (or 2, an earlier layout).");
  }
  if (Has(obj["screen"]) && !(IsNum(obj["screen"]) && obj["screen"].d == SCREEN))
    n.Error("\"screen\" must be 1080: every number is in a 1080-high screen.");
  const PortJson &assets = obj["assets"];
  for (const PortJsonMember &m : assets.members) {
    const PortJson &a = m.second;
    if (!IsObj(a) || (!IsStr(a["art"]) && !IsStr(a["file"]) && !IsStr(a["audio"])))
      n.Error("Asset " + Q(m.first) + " needs \"art\" (the game's), \"file\" (the HUD's own image) or \"audio\" (its own sound).");
    else if (IsStr(a["art"]) && a["art"].s.compare(0, 3, "hd:") != 0 && a["art"].s.compare(0, 3, "sd:") != 0)
      n.Error("Asset " + Q(m.first) + ": \"art\" starts with hd: (Remastered) or sd: (classic).");
    if (IsObj(a) && Has(a["slice"]) && !(IsArr(a["slice"]) && a["slice"].size() == 4))
      n.Error("Asset " + Q(m.first) + ": \"slice\" is [left, top, right, bottom] in the image's pixels.");
    if (IsObj(a) && Has(a["pixelated"]) && a["pixelated"].kind != PortJson::BOOL)
      n.Error("Asset " + Q(m.first) + ": \"pixelated\" is true or false.");
    // Hyperion's: a sheet's frames, or how a video plays (port_gpu_hud.cpp, DrawImage)
    if (IsObj(a) && Has(a["frames"])) {
      const PortJson &fr = a["frames"];
      bool bOk = IsObj(fr);
      for (const char *pszKey : {"cols", "rows", "count", "fps"})
        bOk = bOk && (!Has(fr[pszKey]) || (IsNum(fr[pszKey]) && fr[pszKey].d > 0));
      bOk = bOk && (!Has(fr["loop"]) || fr["loop"].kind == PortJson::BOOL);
      if (!bOk)
        n.Error("Asset " + Q(m.first) + ": \"frames\" is {cols, rows, count, fps, loop}, numbers above 0 and loop true or false.");
    }
    for (const char *pszMap : {"normal", "fx"})
      if (IsObj(a) && Has(a[pszMap]) && !IsStr(a[pszMap]))
        n.Error("Asset " + Q(m.first) + ": " + Q(pszMap) + " is a file, like \"file\".");
  }
  std::map<std::string, std::string> targets;  // "panel" or "panel.layer" -> its kind
  std::set<std::string> ids, slotSeen;
  std::map<std::string, std::vector<std::string>> slotPanel;
  std::map<std::string, std::set<std::string>> panelActions;  // the actions a panel's layers have
  std::vector<std::string> outsiders;
  auto checkBound = [&](const std::string &where, const PortJson &v) {
    if (!IsStr(v) || !HudBound(v.s))
      return;
    std::string err;
    if (scratch.Compile(v.s, &err) < 0)
      n.Error(where + ": " + err + " in " + v.s);
  };
  if (!IsArr(obj["panels"]))
    n.Error("The file needs a \"panels\" list.");
  int i = 0;
  for (const PortJson &p : obj["panels"].items) {
    ++i;
    if (!IsObj(p)) {
      n.Error("Panel " + std::to_string(i) + " is not an object.");
      continue;
    }
    const PortJson &pidv = p["id"];
    if (!IsStr(pidv) || pidv.s.empty()) {
      n.Error("Panel " + std::to_string(i) + " needs an \"id\".");
      continue;
    }
    const std::string pid = pidv.s;
    if (ids.count(pid))
      n.Error("Two panels are called " + Q(pid) + ".");
    ids.insert(pid);
    targets[pid] = "panel";
    const PortJson &between = p["between"];
    const PortJson &outside = p["outside"];
    if (Has(outside)) {
      // in a margin outside the band, from the screen's edge to the band's
      if (outside.Str() != "left" && outside.Str() != "right")
        n.Error("Panel " + Q(pid) + ": \"outside\" is left or right, the margin it fills.");
      if (Has(between))
        n.Error("Panel " + Q(pid) + ": a panel is \"outside\" the band or \"between\" two panels, not both.");
      const PortJson &ov = p["overlap"];
      if (Has(ov) && !(IsArr(ov) && ov.size() == 2 && IsNum(ov.At(0)) && IsNum(ov.At(1))))
        n.Error("Panel " + Q(pid) + ": \"overlap\" is [left, right] in units.");
      outsiders.push_back(pid);
    } else if (Has(between)) {
      if (!(IsArr(between) && between.size() == 2)) {
        n.Error("Panel " + Q(pid) + ": \"between\" is [left panel or \"left\", right panel or \"right\"].");
      } else {
        for (int k = 0; k < 2; ++k) {
          const std::string b = between.At(k).Str();
          const std::string edge = k == 0 ? "left" : "right";
          if (b != edge && !(ids.count(b) && b != pid))
            n.Error("Panel " + Q(pid) + ": \"between\" names " + Q(b) + ", which is not a panel earlier in the file or " + Q(edge) + ".");
        }
      }
      const PortJson &ov = p["overlap"];
      if (Has(ov) && !(IsArr(ov) && ov.size() == 2 && IsNum(ov.At(0)) && IsNum(ov.At(1))))
        n.Error("Panel " + Q(pid) + ": \"overlap\" is [left, right] in units.");
    } else if (p["ax"].Str() != "left" && p["ax"].Str() != "center" && p["ax"].Str() != "right") {
      n.Error("Panel " + Q(pid) + ": \"ax\" is left, center or right.");
    }
    if (p["ay"].Str() != "top" && p["ay"].Str() != "center" && p["ay"].Str() != "bottom")
      n.Error("Panel " + Q(pid) + ": \"ay\" is top, center or bottom.");
    if (Has(p["z"]) && !IsNum(p["z"]))
      n.Error("Panel " + Q(pid) + ": \"z\" is a number; higher is on top.");
    if (Has(p["tip"]) && !In(kTipSides, p["tip"].Str()))
      n.Error("Panel " + Q(pid) + ": \"tip\" is " + Join(kTipSides) + ".");
    for (const char *k : {"x", "y", "w", "h"}) {
      if ((Has(between) || Has(outside)) && (strcmp(k, "x") == 0 || strcmp(k, "w") == 0))
        continue;
      if (!IsNum(p[k]))
        n.Error("Panel " + Q(pid) + " needs a number for " + Q(k) + ".");
    }
    for (const PortJsonMember &m : p.members)
      if (!In(kPanelKeys, m.first))
        n.Warn("Panel " + Q(pid) + ": " + Q(m.first) + " means nothing and is ignored.");
    checkBound("Panel " + Q(pid), p["visible"]);
    std::set<std::string> lids;
    int j = 0;
    for (const PortJson &l : p["layers"].items) {
      ++j;
      std::string where = "Layer " + std::to_string(j) + " of " + Q(pid);
      if (!IsObj(l)) {
        n.Error(where + " is not an object.");
        continue;
      }
      std::vector<std::string> kinds;
      for (const char *k : {"image", "rect", "text", "shape", "slot", "run", "each", "icon", "chart"})
        if (Has(l[k]) && !(l[k].kind == PortJson::BOOL && !l[k].b))
          kinds.push_back(k);
      if (kinds.size() != 1)
        n.Error(where + " needs exactly one of \"image\", \"rect\", \"text\", \"shape\", \"slot\", \"run\", \"each\", \"icon\", \"chart\".");
      // a layer for each player, a unit's icon and a chart are read, and this Hyperion
      // does not draw them yet; their templates are not looked into
      const bool bEach = Has(l["each"]);
      if (Has(l["run"])) {
        const bool bY = l["axis"].Str() == "y";
        const char *const ends[3] = {bY ? "top" : "left", "mid", bY ? "bottom" : "right"};
        if (!IsObj(l["run"]) || !IsStr(l["run"]["mid"])) {
          n.Error(where + ": \"run\" is {\"" + ends[0] + "\": asset, \"mid\": asset, \"" + ends[2] +
                  "\": asset}; the middle repeats and the ends are optional.");
        } else {
          for (const PortJsonMember &e : l["run"].members) {
            if (e.first != ends[0] && e.first != ends[1] && e.first != ends[2])
              n.Error(where + ": a run along " + (bY ? "y" : "x") + " has " + ends[0] + ", " + ends[1] + ", " + ends[2] +
                      ", not " + Q(e.first) + ".");
            else if (assets[e.second.Str().c_str()].kind == PortJson::NONE)
              n.Error(where + ": the run's " + e.first + " names no asset (" + Q(e.second.Str()) + ").");
          }
        }
        if (Has(l["axis"]) && l["axis"].Str() != "x" && l["axis"].Str() != "y")
          n.Error(where + ": \"axis\" is x or y.");
        if (Has(l["runFit"]) && l["runFit"].Str() != "round" && l["runFit"].Str() != "cut")
          n.Error(where + ": \"runFit\" is round or cut.");
        if (Has(l["insertAlign"]) && l["insertAlign"].Str() != "center" && l["insertAlign"].Str() != "bottom" &&
            l["insertAlign"].Str() != "top")
          n.Error(where + ": \"insertAlign\" is center, bottom or top.");
        for (const PortJson &a : l["inserts"].items)
          if (assets[a.Str().c_str()].kind == PortJson::NONE)
            n.Error(where + ": the insert " + Q(a.Str()) + " names no asset.");
      }
      const PortJson &regions = l["regions"];
      if (Has(regions)) {
        if (!IsArr(regions) || regions.size() > 8) {
          n.Error(where + ": \"regions\" is a list of up to eight.");
        } else {
          int r = 0;
          for (const PortJson &rg : regions.items) {
            ++r;
            const std::string rw = where + ", region " + (IsStr(rg["id"]) && !rg["id"].s.empty() ? Q(rg["id"].s) : std::to_string(r));
            if (!IsObj(rg)) {
              n.Error(rw + " is not an object.");
              continue;
            }
            if (!IsStr(rg["id"]) || rg["id"].s.empty())
              n.Error(rw + " needs an \"id\".");
            int nShapes = 0;
            for (const char *k : {"path", "rect", "brush", "pick"})
              nShapes += Has(rg[k]) ? 1 : 0;
            if (nShapes != 1)
              n.Error(rw + " needs exactly one of \"path\", \"rect\", \"brush\", \"pick\".");
            if (Has(rg["path"])) {
              bool bOk = IsArr(rg["path"]) && rg["path"].size() >= 3;
              for (const PortJson &q : rg["path"].items)
                bOk = bOk && IsArr(q) && q.size() == 2;
              if (!bOk)
                n.Error(rw + ": \"path\" is three or more [x, y], as fractions of the layer's box.");
            }
            if (Has(rg["rect"]) && !(IsArr(rg["rect"]) && rg["rect"].size() == 4))
              n.Error(rw + ": \"rect\" is [x, y, w, h], as fractions of the layer's box.");
            if (Has(rg["brush"]) && !IsStr(rg["brush"]))
              n.Error(rw + ": \"brush\" is an image, a file or a data: URL.");
            if (Has(rg["pick"]) && !(IsObj(rg["pick"]) && (IsStr(rg["pick"]["color"]) || rg["pick"]["from"].Str() == "lights" ||
                                                          rg["pick"]["from"].Str() == "glass")))
              n.Error(rw + ": \"pick\" is {\"color\": \"#rrggbb\", \"range\": 0 to 1} or {\"from\": \"lights\" or \"glass\"}.");
            for (const PortJsonMember &m : rg.members)
              if (!In(kRegionKeys, m.first))
                n.Warn(rw + ": " + Q(m.first) + " means nothing and is ignored.");
          }
        }
      }
      if (Has(l["places"])) {
        bool bOk = IsArr(l["places"]);
        for (const PortJson &q : l["places"].items) {
          bOk = bOk && IsArr(q) && q.size() == 4;
          for (const PortJson &v : q.items)
            bOk = bOk && v.kind == PortJson::NUMBER;
        }
        if (!bOk)
          n.Error(where + ": \"places\" is a list of [x, y, w, h] in the slot's own units.");
      }
      if (Truthy(l["shape"])) {
        const PortJson &cs = l["cornerStyle"];
        auto checkCorner = [&](const PortJson &c) {
          if (c.Str() != "round" && c.Str() != "chamfer" && c.Str() != "notch")
            n.Error(where + ": \"cornerStyle\" is round, chamfer or notch.");
        };
        if (IsArr(cs))
          for (const PortJson &c : cs.items) checkCorner(c);
        else if (Has(cs))
          checkCorner(cs);
        const PortJson &cut = l["cutouts"];
        if (Has(cut)) {
          bool bOk = IsArr(cut) && cut.size() <= 4;
          for (const PortJson &c : cut.items)
            bOk = bOk && IsArr(c) && c.size() >= 4;
          if (!bOk)
            n.Error(where + ": \"cutouts\" is up to four [x, y, w, h, radius].");
        }
        if (IsArr(l["fill"]) && l["fill"].size() > 4)
          n.Error(where + ": a shape's \"fill\" has at most four stops.");
        if (Has(l["surface"]) && HudSurfaceOf(l["surface"].Str()) < 0)
          n.Error(where + ": no surface " + Q(l["surface"].Str()) + ". Surfaces: " + HudSurfaceNames() + ".");
        if (Has(l["revealStyle"]) && l["revealStyle"].Str() != "peel" && l["revealStyle"].Str() != "warp")
          n.Error(where + ": \"revealStyle\" is peel or warp.");
        if (Has(l["path"])) {
          if (!IsStr(l["path"])) {
            n.Error(where + ": \"path\" is an SVG path's text (M, L, H, V, Q, C, A, Z).");
          } else {
            const auto &loops = HudFlatten(l["path"].s);
            size_t ne = 0;
            for (const auto &lp : loops)
              ne += lp.size();
            if (loops.empty())
              n.Error(where + ": \"path\" draws nothing.");
            else if (ne > HUD_MAX_EDGES)
              n.Warn(where + ": its path has " + std::to_string(ne) + " edges once its curves are cut up; the shader draws " +
                     std::to_string(HUD_MAX_EDGES) + ".");
          }
        }
        const PortJson &ps = l["pathSize"];
        if (Has(ps) && !(IsArr(ps) && ps.size() == 2 && IsNum(ps.At(0)) && ps.At(0).d > 0 && IsNum(ps.At(1)) && ps.At(1).d > 0))
          n.Error(where + ": \"pathSize\" is [w, h], the frame the path is drawn in.");
        const PortJson &sl = l["pathSlice"];
        if (Has(sl)) {
          bool bOk = IsArr(sl) && (sl.size() == 2 || sl.size() == 4);
          for (const PortJson &v : sl.items)
            bOk = bOk && v.kind == PortJson::NUMBER;
          if (!bOk)
            n.Error(where + ": \"pathSlice\" is [x, y], or a band [x0, y0, x1, y1], in the path's units.");
        }
        if (Has(l["pathFit"]) && l["pathFit"].Str() != "slice" && l["pathFit"].Str() != "stretch" && l["pathFit"].Str() != "fit")
          n.Error(where + ": \"pathFit\" is slice, stretch or fit.");
        if (Has(l["profile"]) && HudProfileOf(l["profile"].Str()) < 0)
          n.Error(where + ": \"profile\" is " + HudProfileNames() + ".");
        if (Has(l["pattern"]) && HudPatternOf(l["pattern"].Str()) < 0)
          n.Error(where + ": \"pattern\" is " + HudPatternNames() + ".");
      }
      if (Has(l["tileEvery"]) && !(IsNum(l["tileEvery"]) && l["tileEvery"].d >= 0))
        n.Error(where + ": \"tileEvery\" is the units between copies, or 0 for one.");
      if (Has(l["tileFit"]) && l["tileFit"].Str() != "center" && l["tileFit"].Str() != "start" && l["tileFit"].Str() != "round")
        n.Error(where + ": \"tileFit\" is center, start or round.");
      if (l["tileEvery"].Num() > 0 && Has(l["slot"]))
        n.Error(where + ": a slot is not tiled; tile the layers around it.");
      if (Has(l["id"])) {
        const std::string lid = ValueText(l["id"]);
        if (lids.count(lid))
          n.Error("Panel " + Q(pid) + " has two layers called " + Q(lid) + ".");
        lids.insert(lid);
        targets[pid + "." + lid] = kinds.empty() ? "" : kinds[0];
        where = "Layer " + Q(pid + "." + lid);
      }
      for (const PortJson &rg : regions.items) {
        if (!IsObj(rg) || !Truthy(rg["id"]))
          continue;
        const std::string rid = rg["id"].Str();
        if (lids.count(rid))
          n.Error("Panel " + Q(pid) + ": the region " + Q(rid) + " has the name of a layer or another region.");
        lids.insert(rid);
        targets[pid + "." + rid] = "region";
      }
      if (Has(l["image"]) && assets[l["image"].Str().c_str()].kind == PortJson::NONE)
        n.Error(where + ": no asset called " + Q(l["image"].Str()) + ".");
      if (Has(l["slot"])) {
        const std::string slot = l["slot"].Str();
        if (HudSlotOf(slot) < 0)
          n.Error(where + ": no slot " + Q(slot) + ". Slots: " + Join(kSlotNames) + ".");
        // the stats slot once for each of its views
        const std::string view = l["view"].Str();
        if (slot == "stats" && view != "side" && view != "popup")
          n.Error(where + ": the stats slot needs a \"view\": side, a column at the edge of the view, or popup, every figure over the middle.");
        const std::string seen = slot == "stats" ? slot + "." + view : slot;
        if (slotSeen.count(seen))
          n.Warn(slot == "stats" ? "The stats slot is placed twice for the " + view + " view; the game draws it in both."
                                 : "The slot " + Q(slot) + " is placed twice; the game draws it in both.");
        slotSeen.insert(seen);
        if (slot == "stats")
          slotSeen.insert(slot);
        for (const char *k : {"tint", "add", "glow", "scan"})
          if (Has(l[k]))
            n.Warn(where + ": a slot takes no " + Q(k) + ". Put it on a layer over the slot.");
        if (Has(l["align"]) && (!In(kButtonSlots, slot) ||
                                (l["align"].Str() != "left" && l["align"].Str() != "center" && l["align"].Str() != "right")))
          n.Error(where + ": \"align\" on a slot places a button's label (left, center or right), on " + Join(kButtonSlots) + ".");
        slotPanel[slot].push_back(pid);
      }
      const PortJson &inset = l["inset"];
      if (Has(inset) && !(IsArr(inset) && inset.size() == 4)) {
        n.Error(where + ": \"inset\" is [left, top, right, bottom].");
      } else if (Has(inset)) {
        if (!Has(inset.At(0)) && (!Has(inset.At(2)) || !IsNum(l["w"])))
          n.Error(where + ": an inset with no left needs a right and a \"w\": it pins the layer to the right edge.");
        if (!Has(inset.At(1)) && (!Has(inset.At(3)) || !IsNum(l["h"])))
          n.Error(where + ": an inset with no top needs a bottom and an \"h\": it pins the layer to the bottom edge.");
      }
      const PortJson &gd = l["grid"];
      if (Has(gd)) {
        if (l["slot"].Str() != "selection")
          n.Warn(where + ": \"grid\" lays out the selection slot; it means nothing on another.");
        if (!(IsObj(gd) && IsArr(gd["cell"]) && gd["cell"].size() == 2 && IsNum(gd["cell"].At(0)) && gd["cell"].At(0).d > 0 &&
              IsNum(gd["cell"].At(1)) && gd["cell"].At(1).d > 0))
          n.Error(where + ": \"grid\" needs \"cell\": [w, h], one wireframe's pitch in the slot's units.");
        if (Has(gd["rows"]) && !(IsNum(gd["rows"]) && gd["rows"].d >= 1 && gd["rows"].d == floor(gd["rows"].d)))
          n.Error(where + ": the grid's \"rows\" is a whole number.");
        const PortJson &cols = gd["cols"];
        if (Has(cols) && !(IsArr(cols) && cols.size() == 2 && cols.At(0).Num() >= 1 && cols.At(1).Num() >= cols.At(0).Num()))
          n.Error(where + ": the grid's \"cols\" is [fewest, most]; the columns grow with the slot's width between them.");
        if (Has(gd["tabs"]) && gd["tabs"].Str() != "left" && gd["tabs"].Str() != "right" && gd["tabs"].Str() != "none")
          n.Error(where + ": the grid's \"tabs\" is left, right or none.");
        if (Has(gd["tabW"]) && !(IsNum(gd["tabW"]) && gd["tabW"].d >= 0))
          n.Error(where + ": the grid's \"tabW\" is the page tabs' width in units.");
      }
      // the players' board, its sides and its rows
      const PortJson &split = l["split"], &rowH = l["rowH"];
      if (Has(split) && (l["slot"].Str() != "players" || split.kind != PortJson::BOOL))
        n.Error(where + ": \"split\" is true or false, on the players slot: true puts the teams either side of its middle.");
      if (Has(rowH) && !bEach && ((l["slot"].Str() != "players" && l["slot"].Str() != "stats") ||
                        !(IsArr(rowH) && rowH.size() == 2 && IsNum(rowH.At(0)) && IsNum(rowH.At(1)) &&
                          rowH.At(0).d > 0 && rowH.At(1).d >= rowH.At(0).d)))
        n.Error(where + ": \"rowH\" is [least, most], a row's height in units, on the players or the stats slot; the rows share the slot's height.");
      // the stats' view and the side view's backing
      const PortJson &backing = l["backing"];
      if (Has(backing) && (l["slot"].Str() != "stats" || !IsNum(backing) || backing.d < 0 || backing.d > 1))
        n.Error(where + ": \"backing\" is 0 to 1, how solid the shade behind the stats' rows is, on the stats slot.");
      else if (Has(backing) && l["view"].Str() == "popup")
        n.Warn(where + ": the popup has no backing of the game's; the layout draws its frame. Its \"backing\" is ignored.");
      if (Has(l["view"]) && l["slot"].Str() != "stats")
        n.Warn(where + ": \"view\" is the stats slot's; it means nothing here and is ignored.");
      if (Truthy(l["blend"]) && l["blend"].Str() != "normal" && l["blend"].Str() != "add")
        n.Error(where + ": \"blend\" is normal or add.");
      if (Has(l["action"])) {
        int nArg = 0;
        if (!IsStr(l["action"]))
          n.Error(where + ": \"action\" is the name of what the button does.");
        else if (Has(l["slot"]))
          n.Warn(where + ": a slot does what its slot does; its \"action\" is ignored.");
        else if (HudActionOf(l["action"].s, &nArg) < 0)
          n.Warn(where + ": no action " + Q(l["action"].s) + "; the button is greyed. Actions: " + HudActionNames() + ".");
        else if (l["action"].s == "vision")
          n.Warn(where + ": \"vision\" is a row's, in an each layer over the players; here it has no player and is greyed.");
        if (IsStr(l["action"]))
          panelActions[pid].insert(l["action"].s);
      }
      // whether the button takes a press, and what a screen reader says it is
      if ((Has(l["enabled"]) || Has(l["label"])) && !Has(l["action"]))
        n.Warn(where + ": \"enabled\" and \"label\" are a button's; with no \"action\" they mean nothing.");
      if (Has(l["label"]) && !IsStr(l["label"]))
        n.Error(where + ": \"label\" is the button's name as a screen reader says it.");
      // a layer standing at the end of another's meter
      const PortJson &ride = l["ride"];
      if (Has(ride)) {
        if (!IsStr(ride) || !lids.count(ride.s) || (Has(l["id"]) && ValueText(l["id"]) == ride.s))
          n.Error(where + ": \"ride\" names a layer earlier in the panel; the layer stands at the end of its meter.");
        if (!Has(l["w"]) || !Has(l["h"]))
          n.Error(where + ": a riding layer needs its own \"w\" and \"h\".");
      }
      if (Has(l["weight"]) && !(IsNum(l["weight"]) && l["weight"].d >= 100 && l["weight"].d <= 900))
        n.Error(where + ": \"weight\" is 100 to 900 (400 is regular, 700 bold).");
      if (Truthy(l["font"]) && !In(kFonts, l["font"].Str()))
        n.Warn(where + ": the font " + Q(l["font"].Str()) + " is not one of the game's (" + Join(kFonts) + ").");
      for (const PortJsonMember &m : l.members) {
        if (bEach && (m.first == "layers" || m.first == "team" || m.first == "more"))
          continue;
        if (HudKeyOf(m.first) < 0 || In(kPanelOnlyKeys, m.first))
          n.Warn(where + ": " + Q(m.first) + " means nothing and is ignored.");
        else if (m.first != "id" && m.first != "note")
          checkBound(where, m.second);
      }
    }
  }
  int t = 0;
  for (const PortJson &tl : obj["timelines"].items) {
    ++t;
    const std::string where = "Timeline " + (IsObj(tl) && Truthy(tl["id"]) ? Q(tl["id"].Str()) : std::to_string(t));
    if (!IsObj(tl)) {
      n.Error(where + " is not an object.");
      continue;
    }
    const std::string on = tl["on"].Str();
    if (HudEventOf(on) < 0)
      n.Error(where + ": no event " + Q(on) + ". \"on\" is one of " + Join(kEventNames) + ".");
    if (on.compare(0, 6, "panel.") == 0 && !ids.count(tl["panel"].Str()))
      n.Error(where + ": a panel event names its \"panel\".");
    if (Has(tl["slot"])) {
      const auto &panels = slotPanel[tl["slot"].Str()];
      if (!(on == "panel.press" && std::find(panels.begin(), panels.end(), tl["panel"].Str()) != panels.end()))
        n.Error(where + ": \"slot\" picks the presses of one slot, on panel.press, and names a slot in its panel.");
    }
    if (Has(tl["action"]) && !(on == "panel.press" && IsStr(tl["action"]) &&
                               panelActions[tl["panel"].Str()].count(tl["action"].s)))
      n.Error(where + ": \"action\" picks the presses of one action, on panel.press, and names one a layer of its panel has.");
    if (Has(tl["group"]) && !(on.compare(0, 6, "group.") == 0 && IsNum(tl["group"]) && tl["group"].d >= 0 &&
                              tl["group"].d <= 9 && tl["group"].d == floor(tl["group"].d)))
      n.Error(where + ": \"group\" (0 to 9) picks one control group's events, on group.recall or group.set.");
    if (Has(tl["view"]) && !((on == "stats.show" || on == "stats.hide") &&
                             (tl["view"].Str() == "side" || tl["view"].Str() == "popup")))
      n.Error(where + ": \"view\" (side or popup) picks one view's events, on stats.show or stats.hide.");
    for (const PortJson &c : tl["cues"].items)
      if (!IsArr(c) || !IsNum(c.At(0)) || !IsStr(assets[c.At(1).Str().c_str()]["audio"]))
        n.Error(where + ": a cue is [ms, \"<audio asset>\", volume].");
    if (Truthy(tl["end"]) && tl["end"].Str() != "release" && tl["end"].Str() != "hold" && tl["end"].Str() != "loop")
      n.Error(where + ": \"end\" is release, hold or loop.");
    if (Has(tl["stops"])) {
      bool bOk = IsArr(tl["stops"]);
      for (const PortJson &id : tl["stops"].items) {
        bool bFound = false;
        for (const PortJson &o : obj["timelines"].items)
          bFound = bFound || (IsObj(o) && o["id"].Str() == id.Str() && IsStr(id));
        bOk = bOk && bFound;
      }
      if (!bOk)
        n.Error(where + ": \"stops\" lists the ids of timelines in the file.");
    }
    int j = 0;
    for (const PortJson &tr : tl["tracks"].items) {
      ++j;
      const std::string tw = where + ", track " + std::to_string(j);
      auto it = targets.find(tr["target"].Str());
      if (it == targets.end()) {
        n.Error(tw + ": nothing called " + Q(tr["target"].Str()) + ". A target is \"panel\" or \"panel.layer\".");
        continue;
      }
      const std::string &kind = it->second;
      const std::string prop = tr["prop"].Str();
      bool bOk;
      std::string can;
      if (kind == "panel") {
        bOk = In(kPanelAnim, prop);
        can = Join(kPanelAnim);
      } else if (kind == "slot") {
        bOk = In(kSlotAnim, prop);
        can = Join(kSlotAnim);
      } else if (kind == "region") {
        static const char *const kRegionAnim[] = {"light", "lightElevation", "lightStrength", "ambient", "lightColor",
                                                  "height", "bevel", "parallax", "specular", "roughness", "reflect",
                                                  "emissive", "grime", "contrast", "saturation", "grade", "lampPulse",
                                                  "lampRate", "lampFlicker", "glassLines", "scan", "shimmer"};
        bOk = In(kRegionAnim, prop);
        can = Join(kRegionAnim);
      } else {
        bOk = In(kLayerAnim, prop);
        can = Join(kLayerAnim);
      }
      if (!bOk)
        n.Error(tw + ": a " + (kind == "panel" ? std::string("panel") : kind == "region" ? std::string("region") : kind + " layer") +
                " cannot move " + Q(prop) + ". It can move " + can + ".");
      if (!IsArr(tr["keys"]) || tr["keys"].items.empty()) {
        n.Error(tw + " needs \"keys\": [[ms, value, easing], ...].");
        continue;
      }
      if (Has(tr["delay"]) && !IsNum(tr["delay"]))
        n.Error(tw + ": \"delay\" is a number of ms.");
      double last = -1;
      for (const PortJson &k : tr["keys"].items) {
        if (!IsArr(k) || !IsNum(k.At(0))) {
          n.Error(tw + ": a key is [ms, value, easing].");
          continue;
        }
        if (k.At(0).d < last)
          n.Error(tw + ": keys go in time order.");
        last = k.At(0).d;
        if (Has(k.At(2)) && HudEaseOf(k.At(2).Str()) < 0)
          n.Error(tw + ": no easing " + Q(k.At(2).Str()) + ". Easings: " + Join(kEasings) + ".");
      }
    }
  }
  // the band, and the panels outside it
  const PortJson &band = obj["band"];
  bool bBanded = IsNum(band) && band.d > 0;
  if (Has(band) && !(IsNum(band) && band.d >= 0))
    n.Error("\"band\" is the most width, in units, the panels are laid out across (1920 is a 16:9 screen's), or 0 for the whole screen.");
  else if (bBanded && band.d < 1440)
    n.Warn("\"band\" is " + NumText(band.d) + " units, narrower than a 4:3 screen (1440), so the panels keep to it on every screen.");
  std::set<std::string> oids;
  if (Has(obj["options"]) && !IsArr(obj["options"])) {
    n.Error("\"options\" is a list of the choices a player has.");
  } else {
    int oi = 0;
    for (const PortJson &o : obj["options"].items) {
      ++oi;
      const std::string where = "Option " + (IsObj(o) && Truthy(o["id"]) ? Q(o["id"].Str()) : std::to_string(oi));
      if (!IsObj(o)) {
        n.Error(where + " is not an object.");
        continue;
      }
      if (!IsStr(o["id"]) || o["id"].s.empty())
        n.Error(where + " needs an \"id\".");
      else if (oids.count(o["id"].s))
        n.Error("Two options are called " + Q(o["id"].s) + ".");
      oids.insert(o["id"].Str());
      if (!IsStr(o["label"]) || o["label"].s.empty())
        n.Error(where + " needs a \"label\", the name the player sees.");
      for (const PortJsonMember &m : o.members)
        if (!In(kOptionKeys, m.first))
          n.Warn(where + ": " + Q(m.first) + " means nothing and is ignored.");
      if (!IsArr(o["values"]) || o["values"].size() < 2) {
        n.Error(where + " needs \"values\", two or more.");
        continue;
      }
      std::set<std::string> vids;
      int vi = 0;
      for (const PortJson &v : o["values"].items) {
        ++vi;
        const std::string vw = where + ", value " + (IsObj(v) && Truthy(v["id"]) ? Q(v["id"].Str()) : std::to_string(vi));
        if (!IsObj(v)) {
          n.Error(vw + " is not an object.");
          continue;
        }
        if (!IsStr(v["id"]) || v["id"].s.empty())
          n.Error(vw + " needs an \"id\".");
        else if (vids.count(v["id"].s))
          n.Error(where + ": two values are called " + Q(v["id"].s) + ".");
        vids.insert(v["id"].Str());
        if (!IsStr(v["label"]) || v["label"].s.empty())
          n.Error(vw + " needs a \"label\".");
        for (const PortJsonMember &m : v.members)
          if (!In(kValueKeys, m.first))
            n.Warn(vw + ": " + Q(m.first) + " means nothing and is ignored.");
        const PortJson &vb = v["band"];
        if (Has(vb) && !(IsNum(vb) && vb.d >= 0))
          n.Error(vw + ": \"band\" is the band's width in units, as the file's \"band\", or 0 for none.");
        if (IsNum(vb) && vb.d > 0)
          bBanded = true;
        if (!Has(v["set"]))
          continue;
        if (!IsObj(v["set"])) {
          n.Error(vw + ": \"set\" is { \"panel\" or \"panel.layer\": { field: value } }.");
          continue;
        }
        for (const PortJsonMember &tg : v["set"].members) {
          if (!targets.count(tg.first))
            n.Warn(vw + ": \"set\" names " + Q(tg.first) + ", which the file does not have; it changes nothing.");
          if (!IsObj(tg.second)) {
            n.Error(vw + ": \"set\" gives " + Q(tg.first) + " an object of fields.");
            continue;
          }
          for (const PortJsonMember &k : tg.second.members) {
            if (In(kSetNever, k.first))
              n.Warn(vw + ": " + Q(tg.first + "." + k.first) + " cannot be set; it is left as the file has it.");
            else if (k.first == "ax" && k.second.Str() != "left" && k.second.Str() != "center" && k.second.Str() != "right")
              n.Error(vw + ": \"ax\" is left, center or right.");
            else if (k.first == "ay" && k.second.Str() != "top" && k.second.Str() != "center" && k.second.Str() != "bottom")
              n.Error(vw + ": \"ay\" is top, center or bottom.");
            else
              checkBound(vw + ", " + Q(tg.first + "." + k.first), k.second);
          }
        }
      }
      if (Has(o["default"]) && !vids.count(o["default"].Str()))
        n.Error(where + ": \"default\" names no value.");
    }
  }
  if (!outsiders.empty() && !bBanded) {
    std::string list;
    for (size_t k = 0; k < outsiders.size(); ++k)
      list += std::string(k ? ", " : "") + Q(outsiders[k]);
    const bool bMany = outsiders.size() > 1;
    n.Warn(std::string(bMany ? "Panels " : "Panel ") + list + (bMany ? " are" : " is") +
           " outside the band, but the file has no \"band\" and no option gives one, so " +
           (bMany ? "they are" : "it is") + " never drawn.");
  }
  // a replay layout, and the one a game layout names to wear in a replay
  const PortJson &mode = obj["mode"], &replay = obj["replay"];
  if (Has(mode) && mode.Str() != "game" && mode.Str() != "replay")
    n.Error("\"mode\" is game or replay: a replay layout is the one worn while a replay plays.");
  if (Has(replay)) {
    // as hudplayer.js tests it: a name ending .json, not from the root, another scheme or the folder above
    std::string r = replay.Str();
    for (char &c : r)
      c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    size_t nScheme = 0;
    while (nScheme < r.size() && r[nScheme] >= 'a' && r[nScheme] <= 'z')
      ++nScheme;
    const bool bJson = r.size() >= 5 && r.compare(r.size() - 5, 5, ".json") == 0;
    if (!IsStr(replay) || !bJson || r[0] == '/' || (nScheme > 0 && nScheme < r.size() && r[nScheme] == ':') ||
        r.compare(0, 2, "..") == 0)
      n.Error("\"replay\" names the layout to wear while a replay plays: a .json file beside this one.");
    if (mode.Str() == "replay")
      n.Warn("A replay layout names a \"replay\" of its own; it is not read.");
  }
  // the viewer's state the layout starts on
  const PortJson &start = obj["start"];
  if (Has(start) && !IsObj(start)) {
    n.Error("\"start\" is the viewer's state as the layout is put on, as {\"stats.side\": true}.");
  } else {
    for (const PortJsonMember &m : start.members) {
      if (m.first == "stats.page") {
        if (!In(kStatsPages, m.second.Str()))
          n.Error("\"start\": \"stats.page\" is " + Join(kStatsPages) + ".");
      } else if (!In(kStartSwitches, m.first)) {
        n.Warn("\"start\": " + Q(m.first) + " means nothing and is ignored. It names " + Join(kStartSwitches) +
               " and stats.page.");
      } else if (m.second.kind != PortJson::BOOL) {
        n.Error("\"start\": " + Q(m.first) + " is true or false.");
      }
    }
  }
  const bool bReplayMode = mode.Str() == "replay";
  std::vector<std::string> lacks;
  for (const char *s : kMultiplayerSlots)
    if (!slotSeen.count(s) && !bReplayMode)
      lacks.push_back(s);
  for (const char *s : kReplaySlots)
    if (!slotSeen.count(s) && bReplayMode)
      lacks.push_back(s);
  const bool bNoPlay = std::find(lacks.begin(), lacks.end(), "play") != lacks.end();
  const bool bNoMenu = std::find(lacks.begin(), lacks.end(), "menu") != lacks.end();
  if (!lacks.empty() && IsArr(obj["panels"]) && bReplayMode) {
    std::string list;
    for (size_t k = 0; k < lacks.size(); ++k)
      list += std::string(k ? ", " : "") + Q(lacks[k]);
    n.Warn("Not fit for replays: the file places no " + list + " slot, so a mouse viewer could not " +
           (bNoPlay ? "pause the replay" : "") + (lacks.size() > 1 ? " or " : "") +
           (bNoMenu ? "open the Game Menu to quit it" : "") + ".");
  } else if (!lacks.empty() && IsArr(obj["panels"])) {
    std::string list;
    for (size_t k = 0; k < lacks.size(); ++k)
      list += std::string(k ? ", " : "") + Q(lacks[k]);
    n.Warn("Not fit for multiplayer: the file places no " + list +
           " slot. Classic StarCraft gives Diplomacy and Messaging no key, so a player could not reach them" +
           (std::find(lacks.begin(), lacks.end(), "menu") != lacks.end() ? ", and a mouse player could not open the Game Menu" : "") + ".");
  }
  if (Has(obj["classic"]) && obj["classic"].kind != PortJson::BOOL)
    n.Error("\"classic\" is true or false: true holds the file to the classic console's slots.");
  if (obj["classic"].kind == PortJson::BOOL && obj["classic"].b)
    for (const std::string &s : slotSeen) {
      const int k = HudSlotOf(s);
      if (k >= 0 && !kSlotClassic[k])
        n.Warn("A classic file: the slot " + Q(s) + " is Hyperion's, not the classic console's.");
    }
}

// ---------------------------------------------------------------------------
// The worn, checked file compiled.

float SaltOf(const std::string &s) {
  int h = 0;
  for (unsigned char c : s)
    h = static_cast<int>(static_cast<unsigned int>(h) * 31u + c);
  return static_cast<float>(h % 1000) / 7.0f;
}

void CompileFields(const PortJson &def, std::vector<HudField> *pFields, HudBindings &b, bool bLayer) {
  for (const PortJsonMember &m : def.members) {
    if (m.first == "id" || m.first == "note" || m.first == "layers" || (bLayer && m.first == "text"))
      continue;
    const int nKey = HudKeyOf(m.first);
    if (nKey < 0)
      continue;
    HudField f;
    f.nKey = nKey;
    f.value = m.second;
    if (IsStr(m.second) && HudBound(m.second.s)) {
      std::string err;
      f.nTemplate = b.Compile(m.second.s, &err);
    }
    pFields->push_back(f);
  }
}

bool ReadKeyValue(const PortJson &v, HudTrackKey *pKey) {
  if (v.kind == PortJson::NUMBER) {
    pKey->d = v.d;
    return true;
  }
  if (IsStr(v) && HudParseColour(v.s, pKey->rgba)) {
    pKey->bColour = true;
    return true;
  }
  if (v.kind == PortJson::BOOL) {
    pKey->d = v.b ? 1 : 0;
    return true;
  }
  pKey->s = v.Str();
  return true;
}

void Compile(const PortJson &hud, HudDesign *d) {
  d->name = hud["name"].Str();
  d->race = hud["race"].Str("terran");
  d->bClassic = hud["classic"].kind == PortJson::BOOL && hud["classic"].b;
  d->bReplayMode = hud["mode"].Str() == "replay";
  d->replay = d->bReplayMode ? std::string() : hud["replay"].Str();
  d->band = IsNum(hud["band"]) && hud["band"].d > 0 ? hud["band"].d : 0;
  for (const PortJsonMember &m : hud["start"].members) {
    int v = -1;
    for (int k = 0; k < 4; ++k)
      if (m.second.Str() == kStatsPages[k])
        v = k;
    if (m.first == "stats.page" ? v >= 0 : In(kStartSwitches, m.first) && m.second.kind == PortJson::BOOL)
      d->start.push_back({m.first, m.first == "stats.page" ? v : m.second.b ? 1 : 0});
  }
  for (const PortJsonMember &m : hud["assets"].members) {
    HudAsset a;
    a.name = m.first;
    a.art = m.second["art"].Str();
    a.file = m.second["file"].Str();
    a.audio = m.second["audio"].Str();
    a.def = m.second;
    d->assets.push_back(a);
  }
  for (const PortJson &p : hud["panels"].items) {
    HudPanelDef pd;
    pd.id = p["id"].Str();
    CompileFields(p, &pd.fields, d->bindings, false);
    const std::string ax = p["ax"].Str(), ay = p["ay"].Str();
    pd.nAx = ax == "center" ? 1 : ax == "right" ? 2 : 0;
    pd.nAy = ay == "center" ? 1 : ay == "bottom" ? 2 : 0;
    pd.x = p["x"].Num();
    pd.y = p["y"].Num();
    pd.w = p["w"].Num();
    pd.h = p["h"].Num();
    const std::string outside = p["outside"].Str();
    pd.nOutside = outside == "left" ? 1 : outside == "right" ? 2 : 0;
    if (pd.nOutside == 0 && IsArr(p["between"]) && p["between"].size() == 2) {
      pd.bBetween = true;
      pd.between[0] = p["between"].At(0).Str();
      pd.between[1] = p["between"].At(1).Str();
    }
    if ((pd.bBetween || pd.nOutside != 0) && IsArr(p["overlap"])) {
      pd.overlap[0] = p["overlap"].At(0).Num();
      pd.overlap[1] = p["overlap"].At(1).Num();
    }
    pd.bClamp = Truthy(p["clamp"]);
    pd.bHover = Truthy(p["hover"]);
    pd.bZ = IsNum(p["z"]);
    pd.z = p["z"].Num();
    const std::string tip = p["tip"].Str("auto");
    for (int k = 0; k < 6; ++k)
      if (tip == kTipSides[k])
        pd.nTip = k;
    pd.bTooltip = p["kind"].Str() == "tooltip";
    pd.bMusic = p["kind"].Str() == "music";
    int i = 0;
    for (const PortJson &l : p["layers"].items) {
      HudLayerDef ld;
      ld.id = Has(l["id"]) ? ValueText(l["id"]) : std::string();
      ld.key = ld.id.empty() ? std::string() : pd.id + "." + ld.id;
      ld.fSalt = SaltOf(pd.id + std::to_string(i));
      ld.kind = Has(l["image"]) ? HUD_IMAGE : Truthy(l["run"]) ? HUD_RUN : Truthy(l["rect"]) ? HUD_RECT
              : Has(l["text"]) ? HUD_TEXT : Truthy(l["shape"]) ? HUD_SHAPE : Has(l["each"]) ? HUD_EACH
              : Has(l["icon"]) ? HUD_ICON : Has(l["chart"]) ? HUD_CHART : HUD_SLOT;
      if (IsStr(l["ride"]))
        for (size_t k = 0; k < pd.layers.size(); ++k)
          if (pd.layers[k].id == l["ride"].s)
            ld.nRide = static_cast<int>(k);
      if (ld.kind == HUD_SLOT)
        ld.nSlot = HudSlotOf(l["slot"].Str());
      else if (IsStr(l["action"]) && (ld.nAction = HudActionOf(l["action"].s, &ld.nActionArg)) < 0)
        ld.nAction = HUD_ACT_UNKNOWN;
      if (ld.kind == HUD_TEXT) {
        std::string err;
        const PortJson &t = l["text"];
        ld.nText = d->bindings.Compile(IsStr(t) ? t.s : ValueText(t), &err);
      }
      if (ld.kind == HUD_SHAPE && l["glass"].Num() > 0) {
        d->bGlass = true;
        d->nGlassPasses = std::max(d->nGlassPasses, static_cast<int>(IsNum(l["glassBlur"]) ? l["glassBlur"].d : 4));
      }
      // a lit image reads the screen behind it as well
      if ((ld.kind == HUD_IMAGE || ld.kind == HUD_RUN) && Has(l["lit"])) {
        d->bGlass = true;
        d->nGlassPasses = std::max(d->nGlassPasses, 3);
      }
      CompileFields(l, &ld.fields, d->bindings, true);
      ld.def = l;
      pd.layers.push_back(ld);
      ++i;
    }
    d->panels.push_back(pd);
  }
  // the style's tooltip, its sizes with the format's defaults
  const PortJson &T = hud["tooltip"];
  if (IsObj(T)) {
    HudTooltip &tp = d->tooltip;
    tp.bOn = true;
    if (IsNum(T["pad"]))
      for (double &p : tp.pad) p = T["pad"].d;
    else if (IsArr(T["pad"]) && T["pad"].size() == 4)
      for (int i = 0; i < 4; ++i) tp.pad[i] = T["pad"].At(i).Num();
    double *const apNum[] = {&tp.frame, &tp.gap, &tp.minWidth, &tp.maxWidth, &tp.corner, &tp.tab, &tp.tabWidth,
                             &tp.stripSize, &tp.stripInset, &tp.keyGap, &tp.icon, &tp.iconGap, &tp.costGap,
                             &tp.markLine, &tp.offset, &tp.margin};
    for (size_t i = 0; i < sizeof apNum / sizeof apNum[0]; ++i)
      if (IsNum(T[kTipNums[i]]))
        *apNum[i] = T[kTipNums[i]].d;
    const std::string cs = T["cornerStyle"].Str("chamfer"), strip = T["strip"].Str("top");
    tp.nCorner = cs == "round" ? 0 : cs == "notch" ? 2 : 1;
    tp.nStrip = strip == "left" ? 1 : strip == "none" ? 2 : 0;
    if (T["rule"].kind == PortJson::BOOL)
      tp.bRule = T["rule"].b;
    if (IsArr(T["keyPad"]) && T["keyPad"].size() == 2)
      for (int i = 0; i < 2; ++i) tp.keyPad[i] = T["keyPad"].At(i).Num();
    if (IsStr(T["mark"]))
      HudParseColour(T["mark"].s, tp.mark);
    if (IsStr(T["short"]))
      HudParseColour(T["short"].s, tp.shortOf);
  }
  std::map<std::string, int> tlIndex;
  for (const PortJson &t : hud["timelines"].items)
    tlIndex[t["id"].Str()] = static_cast<int>(tlIndex.size());
  for (const PortJson &t : hud["timelines"].items) {
    HudTimeline tl;
    tl.id = t["id"].Str();
    tl.nOn = HudEventOf(t["on"].Str());
    tl.panel = t["panel"].Str();
    tl.nSlot = Has(t["slot"]) ? HudSlotOf(t["slot"].Str()) : -1;
    tl.nGroup = IsNum(t["group"]) ? static_cast<int>(t["group"].d) : -1;
    tl.nView = t["view"].Str() == "side" ? 0 : t["view"].Str() == "popup" ? 1 : -1;
    if (IsStr(t["action"]) && (tl.nAction = HudActionOf(t["action"].s, &tl.nActionArg)) < 0)
      tl.nAction = HUD_ACT_UNKNOWN;
    const std::string end = t["end"].Str();
    tl.nEnd = end == "hold" ? HUD_END_HOLD : end == "loop" ? HUD_END_LOOP : HUD_END_RELEASE;
    for (const PortJson &s : t["stops"].items) {
      auto it = tlIndex.find(s.Str());
      if (it != tlIndex.end())
        tl.stops.push_back(it->second);
    }
    for (const PortJson &tr : t["tracks"].items) {
      HudTrack k;
      k.target = tr["target"].Str();
      k.nProp = HudKeyOf(tr["prop"].Str());
      if (k.nProp < 0) {
        // a region's own properties (height, parallax, shimmer) are not layer keys: kept by name
        // in the target, which the lit surface reads
        k.target += "#" + tr["prop"].Str();
      }
      k.delay = tr["delay"].Num();
      for (const PortJson &kv : tr["keys"].items) {
        HudTrackKey key;
        key.t = kv.At(0).Num();
        ReadKeyValue(kv.At(1), &key);
        key.nEase = std::max(0, HudEaseOf(kv.At(2).Str("linear")));
        k.keys.push_back(key);
      }
      if (!k.keys.empty())
        tl.duration = std::max(tl.duration, k.delay + k.keys.back().t);
      tl.tracks.push_back(k);
    }
    for (const PortJson &c : t["cues"].items) {
      HudCue cue;
      cue.t = c.At(0).Num();
      cue.asset = c.At(1).Str();
      cue.volume = IsNum(c.At(2)) ? c.At(2).d : 1.0;
      tl.duration = std::max(tl.duration, cue.t);
      tl.cues.push_back(cue);
    }
    d->timelines.push_back(tl);
  }
}

}  // namespace

int HudKeyOf(const std::string &name) {
  static std::map<std::string, int> s_keys;
  if (s_keys.empty())
    for (int i = 0; i < HK_COUNT; ++i)
      s_keys[kKeyNames[i]] = i;
  auto it = s_keys.find(name);
  return it == s_keys.end() ? -1 : it->second;
}

const char *HudKeyName(int nKey) { return nKey >= 0 && nKey < HK_COUNT ? kKeyNames[nKey] : ""; }

const char *HudSlotName(int nSlot) { return nSlot >= 0 && nSlot < HUD_SLOTS ? kSlotNames[nSlot] : ""; }

int HudSlotOf(const std::string &name) {
  for (int i = 0; i < HUD_SLOTS; ++i)
    if (name == kSlotNames[i])
      return i;
  return -1;
}

// by HudAction, up to the group's; then the layers' own, the replay's, the stats' and vision
const char *const kActionNames[] = {"menu", "diplomacy", "messaging", "terrain", "idle", "army"};
const struct {
  const char *pszName;
  int nAction, nArg;
} kActionsMore[] = {
    {"replay.play", HUD_ACT_REPLAY_PLAY, 0},       {"replay.slower", HUD_ACT_REPLAY_SLOWER, 0},
    {"replay.faster", HUD_ACT_REPLAY_FASTER, 0},   {"replay.tv", HUD_ACT_REPLAY_TV, 0},
    {"replay.follow", HUD_ACT_REPLAY_FOLLOW, 0},   {"replay.seek", HUD_ACT_REPLAY_SEEK, 0},
    {"stats.popup", HUD_ACT_STATS_POPUP, 0},       {"stats.side", HUD_ACT_STATS_SIDE, 0},
    {"stats.page.next", HUD_ACT_STATS_PAGE, HUD_PAGE_NEXT}, {"stats.page.prev", HUD_ACT_STATS_PAGE, HUD_PAGE_PREV},
    {"stats.page.overview", HUD_ACT_STATS_PAGE, 0}, {"stats.page.income", HUD_ACT_STATS_PAGE, 1},
    {"stats.page.army", HUD_ACT_STATS_PAGE, 2},    {"stats.page.lost", HUD_ACT_STATS_PAGE, 3},
    {"vision", HUD_ACT_VISION, 0}};

int HudActionOf(const std::string &name, int *pnArg) {
  *pnArg = 0;
  for (int i = 0; i < static_cast<int>(sizeof kActionNames / sizeof kActionNames[0]); ++i)
    if (name == kActionNames[i])
      return i;
  if (name.size() == 7 && name.compare(0, 6, "group.") == 0 && name[6] >= '0' && name[6] <= '9') {
    *pnArg = name[6] - '0';
    return HUD_ACT_GROUP;
  }
  for (const auto &a : kActionsMore)
    if (name == a.pszName) {
      *pnArg = a.nArg;
      return a.nAction;
    }
  // a seat of the lobby's eight: 1.17's replay vision is a bit a player in a byte
  if (name.size() == 8 && name.compare(0, 7, "vision.") == 0 && name[7] >= '0' && name[7] <= '7') {
    *pnArg = name[7] - '0';
    return HUD_ACT_VISION_SEAT;
  }
  return -1;
}

std::string HudActionNames(void) {
  std::string s = Join(kActionNames) + ", group.0 to group.9";
  for (const auto &a : kActionsMore)
    s += std::string(", ") + a.pszName;
  return s + ", vision.0 to vision.7";
}

int HudActionSlot(int nAction) {
  switch (nAction) {
    case HUD_ACT_MENU: return HUD_SLOT_MENU;
    case HUD_ACT_DIPLOMACY: return HUD_SLOT_DIPLOMACY;
    case HUD_ACT_MESSAGING: return HUD_SLOT_MESSAGING;
    case HUD_ACT_TERRAIN: return HUD_SLOT_TERRAIN;
    case HUD_ACT_IDLE: return HUD_SLOT_IDLE;
    case HUD_ACT_ARMY: return HUD_SLOT_ARMY;
    case HUD_ACT_GROUP: return HUD_SLOT_GROUPS;
    case HUD_ACT_REPLAY_PLAY: return HUD_SLOT_PLAY;
    case HUD_ACT_REPLAY_SLOWER: return HUD_SLOT_SLOWER;
    case HUD_ACT_REPLAY_FASTER: return HUD_SLOT_FASTER;
    case HUD_ACT_REPLAY_TV: return HUD_SLOT_TV;
    case HUD_ACT_REPLAY_FOLLOW: return HUD_SLOT_FOLLOW;
    default: return -1;
  }
}

const char *HudEventName(int nEvent) { return nEvent >= 0 && nEvent < HUD_EVENTS ? kEventNames[nEvent] : ""; }

int HudEventOf(const std::string &name) {
  for (int i = 0; i < HUD_EVENTS; ++i)
    if (name == kEventNames[i])
      return i;
  return -1;
}

int HudEaseOf(const std::string &name) {
  for (int i = 0; i < static_cast<int>(sizeof kEasings / sizeof kEasings[0]); ++i)
    if (name == kEasings[i])
      return i;
  return -1;
}

double HudEase(int nEase, double u) {
  switch (nEase) {
    case 1: return u * u * u;
    case 2: return 1 - pow(1 - u, 3);
    case 3: return u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2;
    case 4: {
      const double c1 = 1.70158, c3 = c1 + 1;
      return 1 + c3 * pow(u - 1, 3) + c1 * pow(u - 1, 2);
    }
    case 5: return u >= 1 ? 1 : 0;
    case 6: return (1 - exp(-5 * u) * cos(11 * u)) / (1 - exp(-5.0) * cos(11.0));
    default: return u;
  }
}

bool HudParseColour(const std::string &c, float rgba[4]) {
  // /^#([0-9a-f]{3}|[0-9a-f]{6}|[0-9a-f]{8})$/i
  if (c.empty() || c[0] != '#' || (c.size() != 4 && c.size() != 7 && c.size() != 9))
    return false;
  for (size_t i = 1; i < c.size(); ++i)
    if (!isxdigit(static_cast<unsigned char>(c[i])))
      return false;
  std::string h = c.substr(1);
  if (h.size() == 3)
    h = std::string() + h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
  rgba[0] = strtol(h.substr(0, 2).c_str(), nullptr, 16) / 255.0f;
  rgba[1] = strtol(h.substr(2, 2).c_str(), nullptr, 16) / 255.0f;
  rgba[2] = strtol(h.substr(4, 2).c_str(), nullptr, 16) / 255.0f;
  rgba[3] = h.size() == 8 ? strtol(h.substr(6, 2).c_str(), nullptr, 16) / 255.0f : 1.0f;
  return true;
}

bool HudLoad(const std::string &layoutText, const std::string &styleText, const HudPicks &picks, HudDesign *pDesign,
             std::vector<std::string> *pErrors, std::vector<std::string> *pWarnings,
             const std::vector<std::string> *pParts) {
  Notes n{pErrors, pWarnings};
  PortJson layout;
  std::string err;
  if (!PortJsonParse(layoutText.c_str(), &layout, &err)) {
    n.Error("The file is not JSON: " + err);
    return false;
  }
  PortJson style;
  const bool bStyle = !styleText.empty();
  if (bStyle) {
    if (!PortJsonParse(styleText.c_str(), &style, &err)) {
      n.Error("The style is not JSON: " + err);
      return false;
    }
    std::vector<std::string> styleErrors;
    Notes sn{&styleErrors, pWarnings};
    ValidateStyle(sn, style);
    if (!styleErrors.empty()) {
      pErrors->insert(pErrors->end(), styleErrors.begin(), styleErrors.end());
      return false;
    }
    if (IsArr(style["lights"]) && style["lights"].size() > 0) {
      for (const PortJson &l : style["lights"].items)
        pDesign->lights.push_back(l.Str());
      pDesign->lampColour = style["palette"][pDesign->lights[0].c_str()].Str();
    }
    if (!picks.light.empty())
      Relight(style, picks.light);
    // the selection boxes' colour, when the player picked one, on the tooltip's lit parts: the
    // colour, 65% of the way to white, and the colour at alpha 70 (hud-format.md, Tooltips)
    float a[4];
    if (!picks.tipAccent.empty() && HudParseColour(picks.tipAccent, a) && IsObj(style["palette"])) {
      auto hex = [](const float *c, double toWhite, const char *pszAlpha) {
        char sz[16];
        int v[3];
        for (int i = 0; i < 3; ++i)
          v[i] = static_cast<int>(floor((c[i] + (1.0 - c[i]) * toWhite) * 255.0 + 0.5));
        snprintf(sz, sizeof sz, "#%02x%02x%02x%s", v[0], v[1], v[2], pszAlpha);
        return std::string(sz);
      };
      PortJson *pPal = Member(style, "palette");
      SetMember(*pPal, "tipAccent", Str(hex(a, 0.0, "")));
      SetMember(*pPal, "tipAccentHot", Str(hex(a, 0.65, "")));
      SetMember(*pPal, "tipAccentGlow", Str(hex(a, 0.0, "70")));
    }
    for (const PortJsonMember &m : style["palette"].members)
      if (m.second.kind == PortJson::STRING)
        pDesign->palette[m.first] = m.second.s;
    MusicSkinOf(style, &pDesign->music);
  }
  for (const PortJson &o : layout["options"].items) {
    HudOption opt;
    opt.id = o["id"].Str();
    opt.label = o["label"].Str();
    opt.help = o["help"].Str();
    opt.def = o["default"].Str();
    for (const PortJson &v : o["values"].items)
      opt.values.push_back({v["id"].Str(), v["label"].Str(), v["help"].Str()});
    pDesign->options.push_back(opt);
  }
  PortJson hud = layout;
  Choose(hud, picks);
  const size_t nErrors = pErrors->size();
  std::vector<PortJson> parts;
  for (size_t i = 0; pParts != nullptr && i < pParts->size(); ++i) {
    parts.emplace_back();
    if (!PortJsonParse((*pParts)[i].c_str(), &parts.back(), &err))
      n.Error("Parts file " + std::to_string(i + 1) + " is not JSON: " + err);
  }
  ExpandParts(n, hud, bStyle ? &style : nullptr, parts);
  if (pErrors->size() != nErrors)
    return false;
  Wear(n, hud, bStyle ? &style : nullptr);
  if (pErrors->size() != nErrors)
    return false;
  HudBindings scratch;
  Validate(n, hud, scratch);
  if (pErrors->size() != nErrors)
    return false;
  Compile(hud, pDesign);
  return true;
}
