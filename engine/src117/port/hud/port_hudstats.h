// port_hudstats.h -- the players' stats a viewer calls up (docs/hud-format.md,
// "The players' stats").
//
// Two views, each on or off by itself, and only in a replay or an observer game: the popup F1
// opens over the middle of the view, every figure of every player in four pages with a graph of
// the page's figure over the game, and the side view Shift+F1 shows at the edge of the view on a
// see-through backing. A HUD design places each in its stats slot, where port_gpu_hud.cpp draws it;
// a view the design does not place, or either with no design on, is drawn here in a place of its
// own, a layer over the game. The figures are read from the game, the graph's every two seconds of
// game time; nothing here changes the game.
#pragma once
#include <string>
#include "../front/port_ui.h"        // UiColor
#include "../platform/port_json.h"  // a stats slot's fields

struct PortHudState;

enum { HUD_STATS_SIDE, HUD_STATS_POPUP, HUD_STATS_VIEWS };
enum { HUD_STATS_OVERVIEW, HUD_STATS_INCOME, HUD_STATS_ARMY, HUD_STATS_LOST, HUD_STATS_PAGES };

// The game's hooks, through the HUD's data layer: a game's console set up (PortHudGameReset), each
// game frame (PortHudFrame), and the values read (PortHudGet), which takes the stats' figures.
void PortHudStatsReset(void);
void PortHudStatsFrame(void);
void PortHudStatsFigures(PortHudState *s);

// The views: whether they may show now (a replay or an observer game), each on or off, and the
// popup's page. Showing or hiding one gives stats.show or stats.hide.
bool PortHudStatsAllowed(void);
bool PortHudStatsOn(int nView);
void PortHudStatsSet(int nView, bool bOn);
void PortHudStatsTurn(int nPage);
// A view or the page was set since the game started: a layout's "start" leaves the stats as they
// were set when it is put on for the game.
bool PortHudStatsTouched(void);

// A stats slot's fields: its view, a row's least and most height, and the side view's backing.
void HudStatsSlot(const PortJson &def, int *pnView, double rowH[2], double *pBacking);

// A view into the UI kit's list, as a slot draws: its box w x h units, its top left at (x, y) in the
// target's pixels and k of them a unit, at fAlpha; look the design's colours (the HUD style's text,
// figure, label and lamp). Nothing is drawn while the view is off.
struct HudStatsAt {
  double x = 0, y = 0, k = 1, w = 0, h = 0;
  float fAlpha = 1.0f;
};
struct HudStatsLook {
  UiColor text, figure, label, lamp;
};
void HudStatsDraw(int nView, const HudStatsAt &at, const HudStatsLook &look, double backing, const double rowH[2],
                  const PortHudState &st);

// The mouse over a design's stats slot (port_hudinput.cpp), at lx, ly of its box w x h: whether
// the point is the slot's (the side view takes only its "+N more" row; the rest is the game's), a
// press there, and its tooltip: an id, -1 for none, its title, its key and the box it is for.
bool HudStatsTakes(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st);
void HudStatsPress(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st);
int HudStatsTip(int nView, double w, double h, const double rowH[2], double lx, double ly, const PortHudState &st,
                std::string *pTitle, std::string *pKey, double q[4]);

// The players' eye, the board's and the stats': an almond with its pupil, filling a square of side
// pixels at (x, y).
void HudStatsEye(float x, float y, float side, UiColor c);
