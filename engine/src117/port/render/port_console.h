// port_console.h -- the console laid out as Remastered's HD one, and the units' wireframes
// (port_console.cpp).
#ifndef PORT_CONSOLE_H
#define PORT_CONSOLE_H

#include <vector>
#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The console laid out as Remastered's HD one (port_console.cpp): its regions toward the
// screen's edges while the frame shows the HD console. PortConsoleArtLoaded reads
// 1.17's image from status_load_console, its skin as PortGpuConsoleArt's; PortConsoleFreed is
// status_free's. PortConsoleShift is a dialog's console origin: nConsoleX, or its
// region's while laid out at the edges. PortConsoleArtX is where the image
// status_get_console_art copies from starts. PortConsoleUpdate lays it out between
// frames (RunGameLoop), PortConsoleUnsplit back as 1.17's (before the view changes).
struct ctrl_t;
void PortConsoleArtLoaded(const unsigned char *pPixels, int nWidth, int nHeight, int nSkin);
void PortConsoleFreed(void);
int PortConsoleShift(const struct ctrl_t *pDlg);
int PortConsoleArtX(void);
int PortConsoleSplit(void);
void PortConsoleUnsplit(void);
void PortConsoleUpdate(void);
// A benchmark's run shows no console: its dialogs hidden, its clicks the map's.
bool PortConsoleHidden(void);
// Whether a mask node is the hidden console's, which the screen's mask leaves out (gds_vidblit.cpp).
bool PortConsoleMaskLeftOut(intptr_t nMask);

// The HUD's console (port_hud.h): with a HUD design on (PortHudPlays as the console
// loads), 1.17's console art and its art panels are not drawn and the map has the view's whole
// height. The console's dialogs live on, each drawing into its own bitmap as in 1.17, without the
// art under it (PortConsoleHudBlank, status_get_console_art's), for the HUD's slots to show
// (PortConsoleHudSource); a held dialog is not put on the screen (sub_4C5630) and takes no mouse
// (dlg_event), the HUD's slots standing in for it. A design chosen or left during a game builds
// the console again between frames (PortConsoleUpdate), as the trainer does, keeping the HUD's
// record of the game (PortConsoleRebuilding) and the selection. Presentation only.
bool PortConsoleHudOn(void);
bool PortConsoleRebuilding(void);
// Whether frame nFrame of a console group (ModArtKind: the command icons or the wireframes) is
// a mod's own that the GPU draws at the HD scale over the console, which leaves it out
// (statcmd.cpp, statdata.cpp, statwire.cpp).
bool PortConsoleModUi(int nKind, unsigned nFrame);
bool PortConsoleHudHeld(const struct ctrl_t *pDlg);
// status_get_console_art: the dialog's new bitmap cleared to 0 for the HUD's console, true; false
// for 1.17's, whose art it copies.
bool PortConsoleHudBlank(struct bmp_t *pBmp);
// What a slot shows of the console: a rectangle of a dialog's bitmap, 8-bit pixels in the game's
// palette, 0 where the dialog drew nothing; nPitch bytes a row.
struct PortHudPixels {
  const unsigned char *pPixels;  // the rectangle's top left
  int nPitch;
  int nWidth, nHeight;
};
enum PortHudSourceKind {
  PORT_HUD_SRC_MINIMAP,    // the minimap's picture
  PORT_HUD_SRC_SELECTION,  // the selection's panel, whole
  PORT_HUD_SRC_PORTRAIT,   // the portrait
  PORT_HUD_SRC_CARD,       // a command button, nIndex 0 to 8 left to right and top to bottom
  PORT_HUD_SRC_RESOURCE,   // the resource counter's icon, nIndex 0 minerals, 1 gas, 2 supply
  PORT_HUD_SRC_BUTTON,     // a console button, nIndex port_hudstate.h's PORT_HUD_BTN_MENU to _MESSAGING
};
// Where a slot's part of the console is: its dialog and the rectangle of it, in the dialog's
// coordinates, right and bottom inside it. False when the game shows none there now.
struct rect16_t;
bool PortConsoleHudPart(int nSource, int nIndex, struct ctrl_t **ppDlg, struct rect16_t *pRect);
// The part's pixels. False when the game shows none there now.
bool PortConsoleHudSource(int nSource, int nIndex, PortHudPixels *p);
// The selection panel's boxes as 1.17 draws them (statdata_slot_frame_draw): a selected unit's
// box behind its wireframe, for a unit in its normal state, and the frames of one unit's slots,
// its upgrades and kills, its queue and its cargo; the queue's arrow and its empty places'
// numbers, and the progress bar's frame (its fill keeps its colour). Each pixel of the panel's
// picture (PORT_HUD_SRC_SELECTION, nWidth by nHeight) that still shows its box is set to 1 in
// pMask, the rest 0. False when no box shows. The other states' unit boxes keep their colours,
// which say something (a hallucination, a parasite).
bool PortConsoleHudSelectionBoxes(unsigned char *pMask, int nWidth, int nHeight);
// The progress bar's frame where its fill hides it: 1.17 draws the full picture over the empty
// one, and a full one with no frame, only its dots (as port_remastered.cpp made it from
// Remastered's files before 0.6.21), shortens the frame as the bar fills. The
// empty picture's edge, two pixels deep, over the fill where the panel's picture is clear, as
// palette indices (-1 none), *pnWidth by *pnHeight from *pnX, *pnY of the selection's picture
// (PORT_HUD_SRC_SELECTION). A HUD design draws it over the panel, so the bar keeps its whole frame;
// PortConsoleHudSelectionBoxes counts it. False when no bar shows or none of it is full.
bool PortConsoleHudBarEdge(std::vector<int> *pPixels, int *pnX, int *pnY, int *pnWidth, int *pnHeight);
// The held dialog the HUD gives a mouse event to (port_hudinput.cpp), which then takes it as
// any dialog does; NULL after.
void PortConsoleHudDeliver(const struct ctrl_t *pDlg);
// A console button's face as 1.17 writes it (port_hudstate.h's PORT_HUD_BTN_MENU to
// PORT_HUD_BTN_MESSAGING), its colour bytes left out, into psz; its length.
int PortConsoleHudLabel(int nButton, char *psz, int nMax);

// The console's layout: 1.17's, or the "console" of the active mod's hud.json, in its data
// folder or else its own (docs/mod-data.md), read again as each console loads: an image for a
// race or for replays, shown in place of the HD console, its empty pixels the map's; whether
// the HD console's regions move to the edges; whether the map is drawn to the foot at 640.
// PortConsoleModArt is status_load_console's: the mod's image for a skin (0 zerg, 1 terran,
// 2 protoss, 3 a replay's) in the record's place, false when it gives none.
struct bmp_t;
bool PortConsoleModArt(int nSkin, struct bmp_t *pArt);

// Settings' Console skin ("consoleSkin": 0 by race, 1 Terran, 2 Protoss, 3 Zerg) in a game you
// play. PortConsoleSkinTake reads it as status_load_console loads a game's console; until the
// next, PortConsoleRace is the race whose art the console's parts load (0 zerg, 1 terran,
// 2 protoss): byte_5264C2, the race played, unless one was chosen. A replay keeps its own
// console. The race's other uses, its advisor's voice and its supply icon, stay the race played.
void PortConsoleSkinTake(void);
int PortConsoleRace(void);
// Settings' Console layout ("consoleLayout", on unless set, read again by PortConsoleSkinTake):
// the HD console's regions move to the screen's edges in a wide view, as long as a mod's
// hud.json does not keep them together.
void PortConsoleEdgesSet(bool bOn);

// The numbers 1.17 writes over the selection panel's icons (statdata.cpp's two icon draws):
// Begin before the font writes one, End after, which keeps the pixels it changed, so that the
// HD console shows them over its own icons as 1.17 drew them.
void PortConsoleSlotTextBegin(const struct ctrl_t *pCtrl);
void PortConsoleSlotTextEnd(const struct ctrl_t *pCtrl);

// A unit type's wireframe as a white picture in a cell of nCell pixels (port_console.cpp).
bool PortWireframeCell(int nType, unsigned char *pRgba, int nPitchPx, int nCell);

// The HUD's selection grid, its cells as 1.17's selection panel draws a selected unit
// (statdata_select_btn_draw), as palette indices, -1 clear, each its frame's own box: a box of the
// command buttons' GRP (StatDataBoxFrame's frame), and a unit's wireframe of grpwire.grp in the
// colours of its hit points and shields. False when the console's art is not loaded.
// PortConsoleLighten is the lighter colour the console's subgroup's box is drawn in, and
// PortConsoleLightRemap the palette's nearest to each colour lightened, for 1.17's own panel.
class CUnit;
bool PortConsoleHudBox(int nFrame, std::vector<int> *pPixels, int *pnWidth, int *pnHeight);
bool PortConsoleHudWire(CUnit *pUnit, std::vector<int> *pPixels, int *pnWidth, int *pnHeight);
// A selected unit's wireframe for the selection panel where grpwire.grp has none: its frames are
// 1.17's units', and Hyperion's selection takes buildings. Theirs is the
// console's large wireframe (wirefram.grp) at half its size, a frame as 1.17's are, so that
// rle_draw_normal draws it in the hit points' colours. False when grpwire.grp has the frame; true
// with a frame of no size when there is no picture.
struct FRAME;
bool PortConsoleSmallWire(unsigned nLooks, FRAME *pFrame);
void PortConsoleLighten(unsigned char *pRgb);
const unsigned char *PortConsoleLightRemap(void);

// The console's art panels as laid out now (statfluf.cpp): the four races' lists, 26 records
// in all, each list ending in a wX of 0xFFFF.
struct fluf_t;
fluf_t *StatFlufPanels();

#endif
