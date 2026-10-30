// handicap.h -- a player's handicap, as StarCraft II's lobby has it
// (docs/modules/gameplay.md, "Handicaps").
//
// A player at a handicap has every unit and building at that share of its type's maximum life
// and shields: 50% to 100% in steps of 10, each slot its own. Damage, energy, armour and
// speed stay the type's. The maximums are rounded to whole points, at least one.
//
// The game reads a type's maximums from units.dat's tables (gxUnitLife, 8.8 fixed point, and
// guwUnitEnergy, the shields in whole points) wherever it needs them: a unit made, healed,
// repaired, regenerating or drawn. Each such read that concerns a player's unit goes through
// HandicapLife or HandicapShields with the unit's owner. In a game without a handicap, every
// game by classic rules among them, both give the table's value back unchanged, so the game
// plays as 1.17 does to the frame. A handicap plays by Hyperion's rules (HX_OPT_HANDICAP,
// hxrules.h), and the replay and the save hold each slot's (port_handicap.h).
#ifndef HANDICAP_H
#define HANDICAP_H

#include "platform.h"

class CUnit;

#define HANDICAP_FULL 100   // a player without one
#define HANDICAP_LEAST 50   // the lowest a player can be given
#define HANDICAP_STEP 10    // the choices' step

// Whether a slot of the game being played has a handicap below 100%, set as it starts
// (HandicapForGame, port_handicap.h).
extern bool g_hxHandicapOn;

// A slot's handicap in the game being played, in percent: 100 for none.
int HandicapOf(int nPlayer);
// The type's maximum life lLife (8.8 fixed point) and maximum shields nShields (whole points)
// at nPlayer's handicap.
LONG HandicapLifeOf(LONG lLife, int nPlayer);
int HandicapShieldsOf(int nShields, int nPlayer);

static inline LONG HandicapLife(LONG lLife, int nPlayer) {
  return g_hxHandicapOn ? HandicapLifeOf(lLife, nPlayer) : lLife;
}
static inline int HandicapShields(int nShields, int nPlayer) {
  return g_hxHandicapOn ? HandicapShieldsOf(nShields, nPlayer) : nShields;
}

// CUnit::SetPlayer, the unit handed from nFrom to its new owner (Mind Control, a rescue, Give
// Units): its life and shields keep their share of the maximums at the new owner's handicap.
void HandicapOwnerChanged(CUnit *pUnit, int nFrom);

#endif
