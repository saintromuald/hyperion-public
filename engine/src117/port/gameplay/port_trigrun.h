// port_trigrun.h -- 1.17's trigger actions run outside a trigger: what the live
// channel's spawn, order and clear and the cutscenes' steps share (port_control.cpp,
// port_cutscene.cpp). Each does to the game what the trigger action of its name does, through
// two of the map's own locations kept for it (the scratch locations, 254 and 255), so the game
// sees the change a trigger makes; the game it runs in is marked as changed
// (PortControlChanged). The transmission and the text show the way those actions do, without a
// trigger to hold. Single-player games only; the callers check.
#ifndef PORT_TRIGRUN_H
#define PORT_TRIGRUN_H

#include <vector>

struct CUnit;

// A unit by its id (its place in the unit table), while it lives; NULL else.
CUnit *PortTrigUnit(int nId);

// The scratch location nSlot (0 or 1) placed on x, y, nRadius out each way; its number, as
// an action names a location.
int PortTrigScratch(int nSlot, int x, int y, int nRadius);

// Whether nUnit (0-227, or a mod's from 256) can be made at x, y for nPlayer, nCount of them.
bool PortTrigCreateFits(int nUnit, int x, int y, int nPlayer, int nCount);
// Units made as Create Unit makes them, nCount (1-64) of nUnit at x, y for nPlayer; the ids
// of those made (their places in the unit table) in *pIds. bChanged false leaves the game
// unmarked, for a benchmark's stress run, which its replay makes again (port_bench.cpp).
void PortTrigCreate(int nUnit, int x, int y, int nPlayer, int nCount, std::vector<int> *pIds,
                    bool bChanged = true);

// The orders the Order action gives: move, patrol, attack.
enum { PORT_TRIG_MOVE = 0, PORT_TRIG_PATROL = 1, PORT_TRIG_ATTACK = 2 };
// nPlayer's units within nRadius of x, y ordered to tx, ty, as Order does.
void PortTrigOrderNear(int nPlayer, int x, int y, int nRadius, int tx, int ty, int nOrder);
// One unit by its id ordered to tx, ty; false when there is no such unit.
bool PortTrigOrderId(int nId, int tx, int ty, int nOrder);
// Every unit of nPlayer removed, as Remove Unit does; how many there were.
int PortTrigRemoveAll(int nPlayer);
// One unit by its id removed, or killed (its death shown); false when there is no such unit.
bool PortTrigRemoveId(int nId, bool bKill);

// The way Transmission shows: nPortrait's portrait talking for nMs, the text under the
// messages as Display Text puts it, the sound (a path in the MPQs or the data folder) and,
// when nFlashId names a unit, that unit flashing with a ping on the minimap. Any of the text,
// the sound and the unit may be left out (NULL, NULL, -1).
void PortTrigTransmission(int nPortrait, int nFlashId, const char *pszText, const char *pszSound, unsigned nMs);
// A line among the game's messages, as Display Text puts it, for nMs (0: as long as the
// action would hold it).
void PortTrigText(const char *pszText, unsigned nMs);
// A sound, as Play WAV plays it.
void PortTrigSound(const char *pszPath);

#endif
