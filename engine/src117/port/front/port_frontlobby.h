// port_frontlobby.h -- the pieces of the design's lobby screens (lobby.css), which the skirmish
// set-up, the replays screen and Load Game share (port_frontlobby.cpp): boxes placed in SC2's
// units, the pulldown, the segmented switch, the tip, the dialog over a screen, and a map's
// picture. Presentation only.
#ifndef PORT_FRONTLOBBY_H
#define PORT_FRONTLOBBY_H

#include <windows.h>
#include <string>
#include <vector>
#include "port_frontkit.h"
#include "../gameplay/port_teams.h"  // TeamLayout

namespace FrontKit {

// 1.17's player colours, in tunit.pcx's order (the units' brightest): the eight slots', and
// the three a row may also choose.
extern const unsigned kSlotColours[11];

void Radius(Decl &b, float r);
// A box's title, in capitals.
void BoxTitle(const char *psz);
// SC2's inner container (lobby.css .lb-box).
void InnerBox(Decl &b, float fRim = 2);
// A floating box at x, y in its parent, w by h in SC2's units (h 0 fits its content).
Decl At(const char *pszId, float x, float y, float w, float h);
void Label(const char *psz, float fSize, UiColor c, UiFace face = UI_FACE_TEXT, float fSpacing = 0.0f);
// A set-up's panel at a place (At), its children top to bottom fGap apart (0 for none).
Decl InBox(const char *pszId, float x, float y, float w, float h, float fGap = 0.0f);
// A row of a set-up's options, fHeight high: its name at the left (OptionName), its control
// at the right.
Decl OptionRow(float fHeight);
// An option's name, its note under it (none for NULL or ""), fTopPad below the row's top.
void OptionName(const char *pszName, const char *pszNote, float fTopPad = 10.0f);
void OptionName(const char *pszName, const std::string &note, float fTopPad = 10.0f);
// A sunken well, as a list sits in.
void Well(Decl &w);

// The pulldown or popover that is open, "" for none.
extern std::string g_lbOpen;

// A tip for the control under the mouse, drawn by DrawTip after the screen's elements.
void TipOn(Clay_ElementId id, const char *pszTitle, const char *pszText);
void DrawTip(void);

// A colour chip.
void Chip(unsigned rgb, float fSide = 18);

// A player slot's pieces (lobby.css), as custom draws: its ground (ScDraw n 1 under the
// mouse, 2 closed, 4 compact), its number's ring, and its avatar (n 1 a computer's). The
// skirmish set-up and the benchmark's players draw their rows with them.
void DrawSlot(float x, float y, float w, float h, void *pCtx);
void DrawNum(float x, float y, float w, float h, void *pCtx);
void DrawAva(float x, float y, float w, float h, void *pCtx);

// The players' rows (lobby.js slotRow), as the skirmish and the benchmark draw them. A row's
// ground, to open: 40 high compact, else 69; a slot no one takes is drawn dimmer.
Decl SlotGround(const char *pszId, int i, bool bCompact, bool bSeated);
// The row's number, i + 1.
void SlotNumber(const char *pszId, int i, bool bCompact);
// A tile of the row's colour, rgb, with a name by it (its team's), fWidth wide.
void SlotShade(const char *pszId, int i, unsigned rgb, const char *pszName, float fWidth = 130);
// A name on one line in fWidth pixels: a size smaller than fSize, down to fMin, before it is cut
// short with an ellipsis.
void TextFitted(const std::string &s, float fSize, float fMin, float fWidth, UiColor c);
// A row's Team pulldown: the teams and "New team", with their chips (kTeamBases).
void TeamChoices(const std::vector<std::string> &names, std::vector<std::string> *pItems,
                 std::vector<unsigned> *pChips);
// The Quick layout pulldown: a map's layouts of teams (not its free for all), each with its
// teams, and in *pWhich its place in ls; "None for this map" when it has none.
void QuickLayoutChoices(const std::vector<TeamLayout> &ls, std::vector<std::string> *pItems,
                        std::vector<int> *pWhich);
// The computer levels, as the pulldowns list them (port_ai.h).
const std::vector<std::string> &LevelNames(void);

// A pulldown (BattlenetSmallPulldown): its face shows the choice, or pszFace, and a click
// opens its list under it; the choice made, or -1. Off, it is greyed (not built yet);
// locked, it shows its choice in Hyperion's gold.
enum { PD_ON, PD_OFF, PD_LOCKED };
int Pulldown(const char *pszId, int nIndex, float fWidth, const std::vector<std::string> &items, int nCur,
             int nState = PD_ON, const unsigned *pChips = NULL, const char *pszFace = NULL, float fListWidth = 0);
// A greyed pulldown showing pszShown, with a tip on what it waits for.
void PulldownLater(const char *pszId, int nIndex, float fWidth, const char *pszShown, const char *pszTitle,
                   const char *pszWhy);
// The open pulldown (or colour picker) closes when the button comes up on nothing: a screen's
// draw ends with this. A release on one of its items has chosen that item already.
void PulldownCloseOutside(void);
// Esc closes the open pulldown. True while one is open, so the screen's other keys wait.
bool PulldownKeys(void);

// The segmented switch (lobby.css .lb-seg): the one clicked, or -1; the choices in nOff (a
// bit each) are greyed, with pszWhy as their tip.
int Segments(const char *pszId, const char *const *ppItems, int nItems, int nCur, unsigned nOff = 0,
             const char *pszWhy = NULL, float fHeight = 34);

// A dialog over the screen (lobby.css .lb-dlg): the blackout, and the frame, w by h, centred,
// from y down, left open for its content. True when the blackout was clicked. nZ is the
// blackout's layer, the frame's the next; a dialog over another takes a higher one.
bool DialogOpen(const char *pszId, float w, float h, float y, int nZ = 70);

// A map's picture: its minimap, its starts and what stands on it, read from its scenario.chk
// by MapPictureRead into a slot of the picture atlas (port_ui.h), which each screen keeps its own.
struct MapStart {
  int nSlot;  // the map's player
  int x, y;   // pixels
};
struct MapResource {
  WORD x, y;     // its centre, in pixels
  bool bGas;     // a geyser, else a mineral field
  WORD wAmount;  // what it holds as the map places it, 1.17's 1,500 or 5,000 where the map says none
};
struct MapPlaced {
  WORD x, y;  // its centre, in pixels
  WORD w, h;  // its size, in pixels (units.dat's bounds)
  BYTE owner;
};
struct MapPicture {
  std::string of;          // what it was read from, "" for nothing
  int nSlot = 0;           // the picture atlas's slot
  bool bMinimap = false;   // the slot holds its minimap
  int w = 0, h = 0, tileset = 0;
  std::vector<MapStart> starts;
  std::vector<MapResource> resources;
  std::vector<MapPlaced> placed;  // its other units: a melee game keeps the neutral ones
  std::vector<WORD> tiles;        // its tiles as the game draws them (MTXM), w by h
};
void MapPictureRead(MapPicture *p, const std::string &of, const BYTE *pChk, DWORD cb);
// The map's minitiles walkable or not, w * 4 by h * 4, as its tileset's VF4 says and 1.17's
// regions take them (the mutators' picture of rich bases); false when the files cannot be read.
bool MapWalkable(const MapPicture &p, std::vector<unsigned char> *pWalk);
// With p->nSlot below 0 MapPictureRead fills in the rest and leaves the atlas alone; this is the
// minimap of the same scenario.chk as RGBA, at most nMaxSide pixels a side (the AI lab's wall
// keeps several maps in one slot, port_labwall.cpp).
bool MapMinimapRgba(const BYTE *pChk, DWORD cb, int nMaxSide, std::vector<unsigned char> *pRgba, int *pnW,
                    int *pnH);
// The quick layouts for a map's first nStarts starts, a player at each (port_teams.h): none
// when pPic is NULL (its starts not known). Worked out again only when the map or the count
// changes; *pbNew, when given, says whether this call worked out any.
const std::vector<TeamLayout> &MapLayouts(const MapPicture *pPic, int nStarts, bool *pbNew = NULL);
// The picture, fW by fH in SC2's units: pPic, when read, over the tileset's colour, or
// pszTitle on it, or "No map" when pszTitle is NULL; its start marks float at nZ, above
// the box that holds it. bAllUnits keeps the players' units too (Use Map Settings).
// A start's mark as a caller wants it: its fill (a team's colour) and the number it shows, a
// player's in place of the map's slot; 0 for the map's.
struct MapMark {
  bool bRgb = false;
  unsigned rgb = 0;
  int nNum = 0;
};
void MapPreview(const char *pszId, const MapPicture *pPic, int nTileset, const char *pszTitle, int nMapW, int nMapH,
                float fW, float fH, int nZ, bool bBig, bool bAllUnits, const std::vector<MapMark> *pMarks = NULL);
// Each frame, before the pictures are laid out.
void MapPreviewFrame(void);
// A part of a map's terrain as the game draws it, from its scenario.chk, pChk, cb bytes: the map
// pixels from x0, y0, w by h, as RGBA, black off the map (Settings' HUD tab's picture). False
// when the CHK or the tileset's files cannot be read.
bool MapTerrainRgba(const BYTE *pChk, DWORD cb, int x0, int y0, int w, int h, std::vector<unsigned char> *pRgba,
                    int *pnMapW = NULL, int *pnMapH = NULL);
// A map's minimap as RGBA, at most nMaxSide pixels a side, from its tileset and tiles (the
// CHK's MTXM); the live channel's map_info writes it as a picture.
bool MinimapRgba(int nTileset, int nWidth, int nHeight, const WORD *pTiles, size_t nTiles, int nMaxSide,
                 std::vector<unsigned char> *pRgba, int *pnW, int *pnH);

}  // namespace FrontKit

#endif
