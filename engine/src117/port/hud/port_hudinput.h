// port_hudinput.h -- the mouse over the HUD. The HUD's panels are the console:
// the map takes no press on them and the cursor is the console's there. A press on a slot is
// 1.17's own on the part of the console the slot shows, given to that dialog at the place 1.17
// has it (port_console.h, PortConsoleHudPart), and a drag begun there stays with it until its
// button is up; the idle, army and group buttons are the HUD's presses (port_hudstate.h).
// Presentation only: each press ends in the handler a click on 1.17's console calls, so a game's
// commands are the ones such a click makes.
#pragma once

#include <string>

struct dlgevt_t;
struct rect32_t;

// console_collision's (cursor.cpp): game pixel x, y is over one of the HUD's panels.
bool PortHudOver(int nX, int nY);
// One of 1.17's mouse events (event_Win.cpp), before the dialogs have it: true when the HUD took
// it, and nothing else is to.
bool PortHudMouse(const dlgevt_t *pEvent);
// ctxhelp_show's (CtxtHelp.cpp): a help bubble 1.17 places at a control of its console, at nX,
// nY, moved off the HUD's panel that shows the control.
struct rect16_t;
void PortHudHelpPlace(int nX, int nY, rect16_t *pRect);
// The cursor where 1.17's console code looks for it (statcmd_update, statdata_update_hover): the
// game's cursor, or with the HUD's console the point the pointer is over in 1.17's console,
// through the slot that shows it, and -1, -1 when it is over none.
void PortHudConsoleCursor(LONG anOut[2]);
// The live channel's hud answer (port_control.cpp): "hits", the layers the mouse finds as last
// drawn, top last, each its panel, its slot's name ("" for none) and its box's bounds in game
// pixels [x0, y0, x1, y1].
std::string PortHudHitsJson(void);
// The HUD style's tooltip in place of one of 1.17's bubbles (docs/hud-format.md,
// "Tooltips"), for a control of its console the HUD shows: a card button's place nPlace (0 to 8),
// or with nPlace -1 the control at game pixel nX, nY. pszName is the name's line as 1.17 writes it,
// its colour bytes kept, the letter after a \x03 marked when bMark; pszKey its key ("" for none);
// pszLines the lines under it, newlines between; anCosts minerals, gas, supply and energy, 0 for
// none, and anRaces the race whose icon each wears (0 Zerg, 1 Terran, 2 Protoss; NULL Terran's).
// False when the HUD draws no tooltip there: 1.17's bubble is shown. PortHudTipHide: gone.
bool PortHudTipShow(int nPlace, int nX, int nY, const char *pszName, bool bMark, const char *pszKey,
                    const char *pszLines, const int anCosts[4], const int anRaces[4] = nullptr);
void PortHudTipHide(void);
// mou_cager's (dlgFatal.cpp): a cage over 1.17's minimap moved to where the HUD shows it.
void PortHudCage(rect32_t *pRect);
