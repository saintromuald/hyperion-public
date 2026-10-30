// port_hudplay.h -- a loaded HUD design played: where its panels go on a screen of a
// shape, what each panel and layer is this frame (the file's values, the bindings read, the
// running timelines laid over them), and what the events start. What the design center's
// player does each frame (hudplayer.js: layout, rectOf, fire, advance, overrides, stateOf,
// boxOf, tiles), without the drawing, which the GPU presenter does from the list this gives
// (render/port_gpu_hud.cpp).
#pragma once

#include <string>
#include <vector>
#include "port_hudfile.h"

// A colour, straight RGBA 0 to 1; bSet false for a field the layer leaves out (null).
struct HudColour {
  float c[4] = {0, 0, 0, 0};
  bool bSet = false;
};

// Numbers that may be left out (null).
struct HudOptNum {
  double d = 0;
  bool bSet = false;
};

// Plain numbers with their defaults (hudplayer.js's LAYER_DEFAULTS), X(name, default).
#define HUD_NUM_FIELDS(X)                                                                                             \
  X(opacity, 1) X(scale, 1) X(scaleX, 1) X(scaleY, 1) X(rotate, 0) X(add, 0) X(glow, 0) X(glowRadius, 12)             \
  X(wipe, 1) X(wipeAngle, 0) X(wipeSoft, 24) X(scanAngle, 0) X(scanWidth, 60) X(scanSpacing, 6)                       \
  X(scanStrength, 0.6) X(flicker, 0) X(shake, 0) X(meter, 1) X(line, 1) X(radius, 0) X(size, 20) X(lineHeight, 1.2)  \
  X(outlineWidth, 2) X(shadowBlur, 0) X(tracking, 0) X(fillAngle, 90) X(glass, 0) X(glassBlur, 4)                     \
  X(glassNoise, 0.03) X(glassStreak, 0) X(bevel, 0) X(bevelStrength, 0.6) X(light, 225) X(innerShadow, 0)             \
  X(innerShadowStrength, 0.5) X(rim, 0) X(rimWidth, 3) X(innerGlowSize, 12) X(outerGlowSize, 12) X(holo, 0)           \
  X(lines, 0) X(lineSpacing, 4) X(energy, 0) X(energyWidth, 6) X(surfaceScale, 1) X(gloss, 0) X(wear, 0)              \
  X(detail, 1) X(surfaceSpeed, 1) X(wobble, 0) X(wobbleScale, 40) X(wobbleSpeed, 0) X(spines, 0)                      \
  X(spineSpacing, 40) X(spineWidth, 0.5) X(spineLean, 0) X(bow, 0) X(smooth, 0) X(segments, 0) X(segmentGap, 8)       \
  X(seams, 0) X(seamWidth, 1.5) X(seamStrength, 1) X(bolts, 0) X(boltInset, 8) X(boltSpacing, 100) X(inlay, 0)        \
  X(inlayDepth, 8) X(inlayGlow, 0) X(reveal, 1) X(insertEvery, 0) X(insertY, 0) X(lightElevation, 45)                 \
  X(lightStrength, 1) X(ambient, 0.35) X(artLight, 225) X(artElevation, 45) X(specular, 0.3) X(roughness, 0.55)       \
  X(reflect, 0.4) X(emissive, 0.6) X(grime, 0) X(grimeScale, 40) X(contrast, 1) X(saturation, 1) X(exposure, 1)       \
  X(lampGlow, 6) X(lampGlowStrength, 0.8) X(lampPulse, 0) X(lampRate, 0.5) X(lampFlicker, 0) X(glassLines, 0)         \
  X(power, 1) X(profileSteps, 2) X(profileAt, 6) X(profileWidth, 3) X(panelLines, 0) X(panelLineInset, 10)            \
  X(panelLineGap, 4) X(panelLineWidth, 1.2) X(seamAngle, 0) X(patternScale, 8) X(patternDepth, 0.8)                   \
  X(patternInset, 0) X(patternAngle, 0) X(patternWall, 0.25) X(scratches, 0) X(scratchScale, 30) X(scratchAngle, 0)   \
  X(scratchWander, 25) X(castShadow, 0) X(elevation, 3) X(shadowSoft, 2) X(tileEvery, 0) X(tileEnd, 0) X(relief, 1)   \
  X(crest, 0) X(edgeWear, 0) X(edgeWearWidth, 2) X(sheen, 0) X(weight, 400)

// Colours with their defaults, X(name, default); "" for none.
#define HUD_COLOUR_FIELDS(X)                                                                                          \
  X(tint, "#ffffff") X(addColor, "#ffffff") X(glowColor, "#5fd0ff") X(scanColor, "#9fe8ff") X(color, "#c9d4dd")       \
  X(outline, "") X(shadow, "") X(glassTint, "#0a1622a0") X(highlight, "#ffffff") X(rimColor, "#ffffff")              \
  X(innerGlow, "") X(outerGlow, "") X(energyColor, "#5fd0ff") X(surfaceA, "") X(surfaceB, "") X(surfaceC, "")        \
  X(boltColor, "#9aa4ad") X(inlayColor, "#f4d273") X(revealColor, "#ffffff00") X(lightColor, "#ffffff")              \
  X(grade, "#ffffff") X(lampColor, "#ffb040") X(sweepColor, "#9fe8ff") X(edgeWearColor, "#8f9aa6")                    \
  X(sheenColor, "#b8c6d4")

// A panel's or a layer's values this frame.
struct HudState {
  bool bVisible = true;
  HudOptNum x, y, w, h;  // as the file gives them; a layer's box falls back on its inset or its panel
  double dx = 0, dy = 0; // the tracks' x and y, which move it from where the file puts it
  double origin[2] = {0.5, 0.5};
#define HUD_NUM_MEMBER(n, d) double n = d;
  HUD_NUM_FIELDS(HUD_NUM_MEMBER)
#undef HUD_NUM_MEMBER
#define HUD_COLOUR_MEMBER(n, d) HudColour n;
  HUD_COLOUR_FIELDS(HUD_COLOUR_MEMBER)
#undef HUD_COLOUR_MEMBER
  HudOptNum scan, lit, pathScale;
  // fills and strokes: a colour or up to four gradient stops (a fill's top to bottom)
  int nFill = 0;
  float fill[4][4] = {};
  int nStroke = 0;
  float stroke[4][4] = {};
  double corners[4] = {0, 0, 0, 0};
  int cornerKinds[4] = {0, 0, 0, 0};  // 0 round, 1 chamfer, 2 notch
  int nCutouts = 0;
  double cutouts[4][5] = {};
  bool bInset = false;
  HudOptNum inset[4];
  double shadowOffset[2] = {1, 1.5};
  int nGlowMode = 0;     // 0 under, 1 over
  int nScanMode = 0;     // 0 band, 1 lines
  int nBlend = 0;        // 0 normal, 1 add
  int nMeterFrom = 0;    // 0 left, 1 right, 2 top, 3 bottom
  int nAlign = 0;        // 0 left, 1 center, 2 right (no align on a slot: centre)
  bool bAlignSet = false;
  int nValign = 1;       // 0 top, 1 middle, 2 bottom
  double wrap = 0;       // 0 none, -1 the box's width, else that width
  int nSurface = 0;      // an index into the shader's surfaces
  bool bSurfaceLive = false;
  int nProfile = 0, nPattern = 0, nBoltStyle = 0;
  int nRevealStyle = 0;  // 0 peel, 1 warp
  bool bMirror = false;
  int nAxis = 0;         // 0 x, 1 y
  int nTileFit = 0;      // 0 center, 1 start, 2 round
  int nPathFit = 0;      // 0 slice, 1 stretch, 2 fit
  const std::string *pPath = nullptr;
  bool bPathSize = false;
  double pathSize[2] = {0, 0};
  int nPathSlice = 0;
  double pathSlice[4] = {0, 0, 0, 0};
  std::string font = "eurostile";
  std::string text;
  // a button's: whether it takes a press, and its name as a screen reader says it
  bool bEnabled = true;
  std::string label;
};

// One thing to draw, in order: a layer of a panel, at its box (units, on the screen) and its
// place in the box's margin, with the transform that places it and its opacity.
struct HudDrawItem {
  int nPanel = -1;
  int nLayer = -1;
  HudLayerKind kind = HUD_RECT;
  int nSlot = -1;
  const HudState *pState = nullptr;
  double box[4] = {0, 0, 0, 0};  // x, y, w, h in the panel's units
  double margin = 0;             // units drawn round the box for light outside it
  double screenAt[2] = {0, 0};   // the box's top left on the screen, units, before the transforms
  double m[6] = {1, 0, 0, 1, 0, 0};  // the box's local units (0,0 its top left) to the screen's units: a, b, c, d, e, f
  double opacity = 1;            // with the panel's and the flicker's
  double ml[6] = {1, 0, 0, 1, 0, 0};  // the box's local units to its panel's: the layer's own placing
  double layerOpacity = 1;       // the layer's own, with its flicker, without the panel's
};

// A panel this frame: shown, its local units (0,0 its top left) to the screen's, its opacity.
struct HudPanelFrame {
  bool bVisible = false;
  double m[6] = {1, 0, 0, 1, 0, 0};
  double opacity = 1;
};

// A text's face in the UI kit (port_ui.h's UiFace) by its font and weight: Chakra Petch's four
// cuts, Michroma for eurostile-ext and michroma, and Saira, regular or semibold, for
// the rest, as the design center draws the game's faces without Remastered's.
int HudFaceOf(const std::string &font, double weight);

// What a tooltip shows (docs/hud-format.md, "Tooltips"): the button's name, the index
// in it of the letter marked as its hotkey (-1 for none), its key as the player reads it, its
// costs (minerals, gas, supply, energy; empty for none) and those the player lacks, and up to
// four lines, "Requires: Barracks".
struct HudTipContent {
  std::string title, hotkey;
  int nMark = -1;
  std::string costs[HUD_TIP_COSTS];
  bool abShort[HUD_TIP_COSTS] = {};
  int anRace[HUD_TIP_COSTS] = {1, 1, 1, 1};  // whose icon a cost wears: 0 Zerg, 1 Terran, 2 Protoss
  std::vector<std::string> lines;
};

class HudPlayer {
 public:
  // A design to play; the player keeps a pointer to it.
  void Load(const HudDesign *pDesign);
  const HudDesign *Design() const { return m_pDesign; }
  // The screen's shape in pixels; LW, the screen's width in units, is 1080 * W / H.
  void Layout(int nWidth, int nHeight);
  double LW() const { return m_lw; }
  void Band(double *pfLeft, double *pfWidth) const;
  // The screen's sides the design covers top to bottom, in units: from each edge, its panels as
  // tall as the screen that reach the edge or one another (a column, its filler); 0 for a side
  // none reaches. Hyperion draws the map beside them (port_view.cpp).
  void Sides(double *pfLeft, double *pfRight) const;
  // A panel's box on the screen, units: left, top, w, h. False for none.
  bool PanelRect(int nPanel, double out[4]) const;
  int PanelIndex(const std::string &id) const;

  // An event: starts the timelines that wait on it; info's panel, slot, group, view and a
  // press's action pick among them. Returns how many started. game.start ends whatever runs.
  int Fire(int nEvent, const std::string &panel = std::string(), int nSlot = -1, int nGroup = -1, int nView = -1,
           int nAction = -1, int nActionArg = 0);
  // The clock: dtMs of the HUD's time passes (not the game's).
  void Advance(double dtMs);
  double Clock() const { return m_clock; }
  // The cues the last Advance passed, for the sound: asset names and volumes.
  struct Cue {
    std::string asset;
    double volume;
  };
  const std::vector<Cue> &Cues() const { return m_cues; }

  // The frame: every panel's and layer's values with the bindings read from env and the
  // timelines over them, and the list to draw, in order.
  const std::vector<HudDrawItem> &Frame(const HudEnv &env);
  // The timelines running, for checks: id and time.
  std::vector<std::pair<std::string, double>> Running() const;

  // The style's tooltip (hudplayer.js's p.tip): shown for the button whose box is at
  // (units on the screen: left, top, w, h) in panel nPanel (-1 for none), laid out for what it
  // shows, placed by that panel's "tip" and shown and hidden by the style's tip.show and tip.hide
  // timelines; changed in place when it shows already. pContent NULL hides it. False when the
  // design has no tooltip or the panel says none: the game's own bubble is then shown.
  bool Tip(const HudTipContent *pContent, const double at[4], int nPanel);
  bool HasTip() const { return m_nTipPanel >= 0; }
  // The music player's plates this frame (2026-10-24, port_hud.h's PortHudMusicPlates): each part's
  // first ones shown at the boxes given, the rest hidden. None while its windows are away.
  void MusicPlates(const struct HudMusicPlate *pPlates, int nPlates);
  // Each panel this frame, by its index, after Frame.
  const std::vector<HudPanelFrame> &PanelFrames() const { return m_panels; }

 private:
  struct Inst {
    int nTl;
    double t;
    double dur;
    int order;
    bool cued;
  };
  struct Over {
    int nProp;  // a HudKey
    HudTrackKey v;
  };
  void PlaceAll();
  void Overrides();
  std::vector<double> m_boxes;  // a panel's layers' boxes this frame, four each, for the layers riding them
  // the tooltip's parts laid out for its content and placed; false when it shows nowhere
  bool LayTip();
  void TipLayer(int nLayer, HudState *s) const;
  void MusicLayer(int nLayer, HudState *s) const;
  int m_nMusicPanel = -1;
  struct MusicAt {
    int nWin, nPart, nIndex;
  };
  std::vector<MusicAt> m_musicLayers;  // per layer of its panel
  struct MusicBox {
    double b[4];
    double opacity;
  };
  // per window (the menu's last) and part: the plates given, in their panel's box
  std::vector<std::vector<MusicBox>> m_musicPlates[HUD_MUSIC_WINDOWS + 1];
  // the panel's box: round the plates, as a run is drawn in its panel's box (port_gpu_hud.cpp)
  double m_musicRect[4] = {0, 0, 1, 1};
  struct TipPart {
    double b[4] = {0, 0, 0, 0};
    bool bText = false;
    std::string text;
    bool bColour = false;
    float colour[4] = {0, 0, 0, 1};
  };
  int m_nTipPanel = -1;
  int m_nTipPlate = -1, m_nTipPane = -1, m_nTipMarkLine = -1;
  double m_px = 1;           // pixels a unit, for measuring the tooltip's text as it is drawn
  bool m_bTipNow = false;    // a tooltip is wanted
  bool m_bTipShown = false;  // its panel shows, going away or not
  double m_tipHideAt = -1;   // the clock it goes at, once hidden
  HudTipContent m_tip;
  double m_tipAt[4] = {0, 0, 0, 0};
  int m_nTipOwner = -1;
  std::string m_tipPath;
  std::vector<TipPart> m_tipParts;  // per layer of the tooltip's panel
  std::vector<bool> m_tipHas;
  const HudDesign *m_pDesign = nullptr;
  double m_lw = 1920;
  double m_clock = 0;
  int m_order = 0;
  std::vector<Inst> m_insts;
  std::vector<Cue> m_cues;
  std::vector<std::vector<double>> m_rects;  // per panel: left, top, w, h
  // per panel and per layer: the base state (fixed fields) and this frame's
  std::vector<HudState> m_panelBase, m_panelState;
  std::vector<std::vector<HudState>> m_layerBase, m_layerState;
  std::vector<std::vector<HudState>> m_tileStates;  // tiled copies, made per frame
  std::vector<std::vector<Over>> m_panelOver;
  std::vector<std::vector<std::vector<Over>>> m_layerOver;
  // each track's target: panel and layer (-1 the panel itself), or -1 for none
  std::vector<std::vector<std::pair<int, int>>> m_trackTarget;
  std::vector<HudDrawItem> m_draw;
  std::vector<HudPanelFrame> m_panels;
};

// A field set on a state from a value as the file has it, a binding's result or a track's.
void HudStateSet(HudState *s, int nKey, const PortJson &v);
void HudStateSetVal(HudState *s, int nKey, const HudVal &v);
void HudStateSetKey(HudState *s, int nKey, const HudTrackKey &v);
// A state's values as a number that changes when any of them does: what the layer draws
// (bPlacing false leaves out where it is put: its moves, turn, scale, origin, opacity, shake and
// flicker), for the presenter's caches.
unsigned long long HudStateHash(const HudState &s, bool bPlacing);
