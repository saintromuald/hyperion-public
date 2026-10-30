// port_hudfile.h -- a HUD file read, checked and made ready to play (docs/hud-format.md):
// version 3's panels, layers, slots, bindings, timelines and options, worn in a style. The
// steps are the design center's player's (hudplayer.js): the style checked (validateStyle) and
// lit in the player's light colour (relight), the player's option picks laid over the layout
// (choose), the style put on it (wear), the result checked (validate), then compiled here into
// panels and layers whose fixed fields are read once and whose bound ones each frame.
#pragma once

#include <map>
#include <string>
#include <vector>
#include "port_hudbind.h"
#include "../platform/port_json.h"

// Every field a panel or a layer may have, hudplayer.js's PANEL_KEYS and LAYER_KEYS in one list.
// X(name)
#define HUD_KEYS(X)                                                                                                    \
  X(id) X(kind) X(ax) X(ay) X(x) X(y) X(w) X(h) X(visible) X(clamp) X(origin) X(hover) X(layers) X(opacity) X(scale)  \
  X(scaleX) X(scaleY) X(rotate) X(shake) X(flicker) X(note) X(between) X(overlap) X(z)                                \
  X(image) X(rect) X(text) X(slot) X(inset) X(tint) X(add) X(addColor) X(glow) X(glowColor) X(glowRadius) X(glowMode) \
  X(wipe) X(wipeAngle) X(wipeSoft) X(scan) X(scanMode) X(scanAngle) X(scanWidth) X(scanSpacing) X(scanColor)          \
  X(scanStrength) X(blend) X(crop) X(meter) X(meterFrom) X(fill) X(stroke) X(line) X(radius) X(font) X(size) X(color) \
  X(align) X(valign) X(wrap) X(lineHeight) X(shadow) X(outline) X(outlineWidth) X(shadowBlur) X(shadowOffset)         \
  X(tracking) X(shape) X(corners) X(cornerStyle) X(cutouts) X(fillAngle) X(glass) X(glassTint) X(glassBlur)           \
  X(glassNoise) X(glassStreak) X(bevel) X(bevelStrength) X(light) X(highlight) X(innerShadow) X(innerShadowStrength)  \
  X(rim) X(rimWidth) X(rimColor) X(innerGlow) X(innerGlowSize) X(outerGlow) X(outerGlowSize) X(holo) X(lines)         \
  X(lineSpacing) X(energy) X(energyColor) X(energyWidth) X(look) X(material) X(surface) X(surfaceA) X(surfaceB)       \
  X(surfaceC) X(surfaceScale) X(gloss) X(wear) X(detail) X(surfaceSpeed) X(mirror) X(wobble) X(wobbleScale)           \
  X(wobbleSpeed) X(spines) X(spineSpacing) X(spineWidth) X(spineLean) X(bow) X(smooth) X(segments) X(segmentGap)      \
  X(seams) X(seamWidth) X(seamStrength) X(bolts) X(boltInset) X(boltSpacing) X(boltColor) X(inlay) X(inlayDepth)      \
  X(inlayGlow) X(inlayColor) X(reveal) X(revealStyle) X(revealColor) X(run) X(axis) X(runFit) X(inserts)              \
  X(insertEvery) X(insertAlign) X(insertY) X(places) X(lit) X(lightElevation) X(lightStrength) X(ambient)             \
  X(lightColor) X(artLight) X(artElevation) X(specular) X(roughness) X(reflect) X(emissive) X(grime) X(grimeScale)    \
  X(contrast) X(saturation) X(grade) X(exposure) X(lampColor) X(lampGlow) X(lampGlowStrength) X(lampPulse)            \
  X(lampRate) X(lampFlicker) X(glassLines) X(sweepColor) X(power) X(regions) X(path) X(pathSize) X(pathFit)           \
  X(pathScale) X(pathSlice) X(profile) X(profileSteps) X(profileAt) X(profileWidth) X(panelLines) X(panelLineInset)   \
  X(panelLineGap) X(panelLineWidth) X(seamAngle) X(pattern) X(patternScale) X(patternDepth) X(patternInset)           \
  X(patternAngle) X(patternWall) X(scratches) X(scratchScale) X(scratchAngle) X(scratchWander) X(castShadow)          \
  X(elevation) X(shadowSoft) X(tileEvery) X(tileEnd) X(tileFit) X(relief) X(crest) X(grid) X(edgeWear)               \
  X(edgeWearWidth) X(edgeWearColor) X(sheen) X(sheenColor) X(boltStyle) X(action) X(tip) X(weight) X(split)       \
  X(rowH) X(view) X(backing) X(enabled) X(label) X(ride) X(each) X(team) X(more) X(order) X(gap) X(teamGap)          \
  X(teamH) X(splitGap) X(max) X(icon) X(chart) X(chartPad) X(gridLines) X(dot)

enum HudKey {
#define HUD_KEY_ENUM(n) HK_##n,
  HUD_KEYS(HUD_KEY_ENUM)
#undef HUD_KEY_ENUM
  HK_COUNT
};
// A field's key by name, or -1; its name.
int HudKeyOf(const std::string &name);
const char *HudKeyName(int nKey);

// Hyperion's own: a layer repeated for each player (its templates, port_hudeach.cpp), a unit's icon, a
// figure charted over the game.
enum HudLayerKind { HUD_IMAGE, HUD_RUN, HUD_RECT, HUD_TEXT, HUD_SHAPE, HUD_SLOT, HUD_EACH, HUD_ICON, HUD_CHART };

// The slots, hudplayer.js's SLOTS in its order. A replay's are 1.17's three replay
// buttons, Hyperion's camera and the players' board, and the players' stats a viewer calls up.
enum HudSlot {
  HUD_SLOT_MINIMAP, HUD_SLOT_SELECTION, HUD_SLOT_PORTRAIT, HUD_SLOT_CARD, HUD_SLOT_RESOURCES, HUD_SLOT_TIME,
  HUD_SLOT_APM, HUD_SLOT_GROUPS, HUD_SLOT_IDLE, HUD_SLOT_CHAT, HUD_SLOT_ALERTS, HUD_SLOT_ARMY, HUD_SLOT_MENU,
  HUD_SLOT_DIPLOMACY, HUD_SLOT_MESSAGING, HUD_SLOT_TERRAIN, HUD_SLOT_PLAY, HUD_SLOT_SLOWER, HUD_SLOT_FASTER,
  HUD_SLOT_TV, HUD_SLOT_FOLLOW, HUD_SLOT_PLAYERS, HUD_SLOT_STATS, HUD_SLOTS
};
const char *HudSlotName(int nSlot);
int HudSlotOf(const std::string &name);

// The events a timeline starts on, hudplayer.js's EVENTS in its order.
enum HudEvent {
  HUD_EV_GAME_START, HUD_EV_GAME_VICTORY, HUD_EV_GAME_DEFEAT, HUD_EV_SELECT_CHANGE, HUD_EV_ALERT_ATTACK,
  HUD_EV_ALERT_MINERALS, HUD_EV_ALERT_GAS, HUD_EV_ALERT_SUPPLY, HUD_EV_RESEARCH_DONE, HUD_EV_UNIT_DONE,
  HUD_EV_PANEL_HOVER, HUD_EV_PANEL_LEAVE, HUD_EV_PANEL_PRESS, HUD_EV_CONSOLE_IN, HUD_EV_CONSOLE_OUT,
  HUD_EV_SELECTION_PAGE, HUD_EV_SELECTION_SUBGROUP, HUD_EV_GROUP_RECALL, HUD_EV_GROUP_SET, HUD_EV_TIP_SHOW,
  HUD_EV_TIP_HIDE, HUD_EV_REPLAY_PAUSE, HUD_EV_REPLAY_RESUME, HUD_EV_REPLAY_SPEED, HUD_EV_REPLAY_VISION,
  HUD_EV_STATS_SHOW, HUD_EV_STATS_HIDE, HUD_EV_REPLAY_SEEK, HUD_EVENTS
};
const char *HudEventName(int nEvent);
int HudEventOf(const std::string &name);

// What a button does (Hyperion's addition, docs/hud-format.md, "Buttons" and
// "Actions"): any layer but a slot may have an "action", 1.17's console buttons, the HUD's own, a
// control group, the replay's buttons and its position, the stats' views and pages, a player's
// vision. HUD_ACT_UNKNOWN is a name the engine does not know: the design plays and the button is
// greyed.
enum HudAction {
  HUD_ACT_MENU, HUD_ACT_DIPLOMACY, HUD_ACT_MESSAGING, HUD_ACT_TERRAIN, HUD_ACT_IDLE, HUD_ACT_ARMY, HUD_ACT_GROUP,
  HUD_ACT_REPLAY_PLAY, HUD_ACT_REPLAY_SLOWER, HUD_ACT_REPLAY_FASTER, HUD_ACT_REPLAY_TV, HUD_ACT_REPLAY_FOLLOW,
  HUD_ACT_REPLAY_SEEK, HUD_ACT_STATS_POPUP, HUD_ACT_STATS_SIDE, HUD_ACT_STATS_PAGE, HUD_ACT_VISION,
  HUD_ACT_VISION_SEAT, HUD_ACT_UNKNOWN
};
// The stats' page actions' arguments: the next page, the one before, then the pages by name.
enum { HUD_PAGE_NEXT = -1, HUD_PAGE_PREV = -2 };
// An action by name, "group.0" to "group.9" giving *pnArg the group, "vision.0" to "vision.7" the
// seat, "stats.page.<page>" the page (0 overview to 3 lost, or HUD_PAGE_NEXT or _PREV); or -1.
int HudActionOf(const std::string &name, int *pnArg);
std::string HudActionNames(void);
// The slot whose setting and showing an action follows: a hidden army slot hides an army
// button too (HUD_SLOT_GROUPS for a group).
int HudActionSlot(int nAction);

// A field as the file has it: its key, its value, and its binding's template when the value
// is a string with {...} in it (-1 otherwise).
struct HudField {
  int nKey = -1;
  PortJson value;
  int nTemplate = -1;
};

struct HudLayerDef {
  std::string id;
  std::string key;  // "panel.layer", or empty for a layer without an id
  HudLayerKind kind = HUD_RECT;
  int nSlot = -1;
  float fSalt = 0.0f;  // the noise's seed for its flicker and shake
  int nText = -1;      // text: its template
  int nAction = -1;    // a button's (HudAction), or -1
  int nActionArg = 0;  // its group, seat or page
  int nRide = -1;      // the layer of its panel whose meter's end it stands at, or -1
  std::vector<HudField> fields;  // every field but id, note and text
  PortJson def;        // the worn layer as a whole, for the fields read as a whole (path, places, grid)
};

struct HudPanelDef {
  std::string id;
  std::vector<HudField> fields;  // every field but id, note and layers
  std::vector<HudLayerDef> layers;
  // what the layout reads, fixed once the options are laid over
  int nAx = 0, nAy = 0;  // 0 left/top, 1 center, 2 right/bottom
  double x = 0, y = 0, w = 0, h = 0;
  bool bBetween = false;
  std::string between[2];
  int nOutside = 0;  // the margin outside the band it fills: 0 none, 1 left, 2 right
  double overlap[2] = {0, 0};
  bool bClamp = false;
  bool bHover = false;
  double z = 0;
  bool bZ = false;
  int nTip = 0;           // its buttons' tooltips: 0 auto, 1 above, 2 below, 3 left, 4 right, 5 none
  bool bTooltip = false;  // the panel a style's tooltip adds
  bool bMusic = false;    // the panel of the music player's plates a style's "musicPlayer" adds
};

// A key of a track: its time, its value (a number or a colour), the easing into it.
struct HudTrackKey {
  double t = 0;
  bool bColour = false;
  double d = 0;
  float rgba[4] = {0, 0, 0, 1};
  std::string s;  // a value that is neither, which steps
  int nEase = 0;
};

struct HudTrack {
  std::string target;  // "panel" or "panel.layer" (or a region)
  int nProp = -1;      // a HudKey
  double delay = 0;
  std::vector<HudTrackKey> keys;
};

struct HudCue {
  double t = 0;
  std::string asset;
  double volume = 1;
};

enum HudEnd { HUD_END_RELEASE, HUD_END_HOLD, HUD_END_LOOP };

struct HudTimeline {
  std::string id;
  int nOn = -1;
  std::string panel;
  int nSlot = -1;   // panel.press: only this slot's presses
  int nGroup = -1;  // group.*: only this group's
  int nView = -1;   // stats.*: only this view's, 0 the side view, 1 the popup
  int nAction = -1;  // panel.press: only a press of this action, with its argument
  int nActionArg = 0;
  int nEnd = HUD_END_RELEASE;
  std::vector<int> stops;  // the timelines it ends, by index
  std::vector<HudTrack> tracks;
  std::vector<HudCue> cues;
  double duration = 0;
};

struct HudAsset {
  std::string name;
  std::string art, file, audio;  // one of them
  PortJson def;
};

// An option the player sets (Settings), and its values.
struct HudOptionValue {
  std::string id, label, help;
};
struct HudOption {
  std::string id, label, help, def;
  std::vector<HudOptionValue> values;
};

// A style's tooltip (docs/hud-format.md, "Tooltips"): its sizes in units, with the
// format's defaults. The parts are the "tooltip" panel's layers, dressed in the style's tip looks.
enum { HUD_TIP_COSTS = 4, HUD_TIP_LINES = 4 };  // minerals, gas, supply, energy
struct HudTooltip {
  bool bOn = false;
  double pad[4] = {12, 9, 12, 10};
  double frame = 4, gap = 6, minWidth = 0, maxWidth = 360, corner = 6;
  int nCorner = 1;  // 0 round, 1 chamfer, 2 notch
  double tab = 6, tabWidth = 18;
  int nStrip = 0;   // 0 top, 1 left, 2 none
  double stripSize = 2, stripInset = 10;
  bool bRule = true;
  double keyPad[2] = {6, 2};
  double keyGap = 14, icon = 15, iconGap = 5, costGap = 14;
  float mark[4] = {0.498f, 0.863f, 1.0f, 1.0f};
  double markLine = 1.5;
  float shortOf[4] = {1.0f, 0.478f, 0.416f, 1.0f};
  double offset = 6, margin = 8;
};

// The music player's skin (2026-10-24, docs/hud-format.md, "The music player"): a style's
// "musicPlayer" block as worn, after the player's light. Each colour by its name, "#rrggbb" or
// "#rrggbbaa"; each part's look whole (its material's values, the style's "shape", the look and the
// part's "set", palette names swapped in) for the shape shader; each role's font and weight; each
// moment's audio asset, "" for silence. bOn false: the style has no block, and the player keeps its
// own skin.
struct HudMusicSkin {
  bool bOn = false;
  std::map<std::string, std::string> colors;
  std::map<std::string, PortJson> materials;  // frame, title, button, lit, menu, shade
  std::map<std::string, std::pair<std::string, double>> fonts;  // lcd, text, title: font, weight
  std::map<std::string, std::string> sounds;  // open, close, shade, denied
};

// The music player's plates: the parts a style's look frames, each drawn by the HUD's shader at a box
// the player's windows give each frame (port_hud.h, PortHudMusicPlates). The style's "music" panel
// has, for each of its three windows from the lowest, as many layers of each part as a window shows
// at once, the window's in a run so a window above covers all of one below; then the menus', one a
// submenu open. A shaded window is its bar alone, on the shade plate.
enum { HUD_MUSIC_FRAME, HUD_MUSIC_TITLE, HUD_MUSIC_BUTTON, HUD_MUSIC_LIT, HUD_MUSIC_MENU, HUD_MUSIC_SHADE, HUD_MUSIC_PARTS };
enum { HUD_MUSIC_WINDOWS = 3 };
const int kHudMusicPlates[HUD_MUSIC_PARTS] = {1, 1, 14, 6, 4, 1};  // a window's, and the menus'
const char *const kHudMusicParts[HUD_MUSIC_PARTS] = {"frame", "title", "button", "lit", "menu", "shade"};
struct HudMusicPlate {
  int nWin;        // 0 to 2 from the lowest; the menu's is its own
  int nPart;
  double box[4];   // the screen's units: left, top, w, h
  double opacity;
};

// A design loaded: the file worn in its style, with the player's picks and light.
struct HudDesign {
  std::string name;
  std::string race;
  bool bClassic = false;
  // a replay layout ("mode": "replay"), worn while a replay plays; and the replay layout a
  // game layout names, a .json file beside it ("" for none: the layout is worn in replays too)
  bool bReplayMode = false;
  std::string replay;
  double band = 0;  // the most width, in units, the panels are laid out across; 0 the screen's
  std::vector<HudPanelDef> panels;
  std::vector<HudTimeline> timelines;
  std::vector<HudAsset> assets;
  std::string files;  // where its own files are, a "file" or "audio" asset's folder (port_hud.cpp)
  std::vector<HudOption> options;  // the layout's own, before the picks
  std::vector<std::string> lights;  // the style's light names, the lamp first
  std::string lampColour;           // the style's lamp, before any relight
  // the style's palette as worn, after the player's light: the colours the slots are dressed in
  // (text, figure, label, accent, lamp, lcd, lcdOff, alert), "#rrggbb" or "#rrggbbaa"
  std::map<std::string, std::string> palette;
  HudBindings bindings;
  bool bGlass = false;  // a layer reads the screen behind it
  int nGlassPasses = 0;
  HudTooltip tooltip;   // the style's, when it has one
  HudMusicSkin music;   // the music player's skin in the style, when it has one
  // the viewer's state the layout starts on, each time a game or a replay puts it on
  // ("stats.side", "stats.popup", "replay.tv", "replay.follow", "minimap.terrain": 0 or 1;
  // "stats.page": 0 overview to 3 lost)
  std::vector<std::pair<std::string, int>> start;
};

// What the player picked for a design: its options and its light colour ("#rrggbb", or empty
// for the style's own).
struct HudPicks {
  std::vector<std::pair<std::string, std::string>> options;
  std::string light;
  // the selection boxes' colour ("#rrggbb"), which the tooltip's lit parts take (tipAccent,
  // tipAccentHot, tipAccentGlow); empty for the style's own
  std::string tipAccent;
};

// Reads and checks a layout and its style (either may be empty text for none). Errors stop
// the load; warnings do not. The design is filled when there are no errors.
// pParts: the texts of the parts files beside the layout (a package's parts\*.json), whose parts
// a design may use as it uses its own (docs/hud-format.md, "Parts").
bool HudLoad(const std::string &layoutText, const std::string &styleText, const HudPicks &picks, HudDesign *pDesign,
             std::vector<std::string> *pErrors, std::vector<std::string> *pWarnings,
             const std::vector<std::string> *pParts = nullptr);

// The parts that use the format's tables elsewhere.
bool HudParseColour(const std::string &s, float rgba[4]);
int HudEaseOf(const std::string &name);  // -1 for none
double HudEase(int nEase, double u);
