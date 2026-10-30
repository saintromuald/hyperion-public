// port_eud.h -- EUD maps: triggers that read and write the
// game's memory through the death counts' unchecked indexes, by 1.16.1's addresses. Hyperion
// plays them as StarCraft: Remastered does. As a map's triggers load, each is tested as
// Remastered tests it; a new game on a map with such a trigger plays by Hyperion's rules with
// HX_OPT_EUD, which its replay and save record. In that game every death count goes through a
// map of 1.16.1's addresses onto Hyperion's own data, also the counts in range. An address the
// map does not serve is logged to hyperion_eud.log and, as in Remastered, stops the triggers;
// HYPERION_EUD=continue drops the access and goes on instead, to learn all a map reaches, up
// to 10,000 addresses not served, and HYPERION_EUD=trace does so and also logs each trigger
// node run, the first 2,000,000.
#ifndef PORT_EUD_H
#define PORT_EUD_H

#include <windows.h>
#include "hxrules.h"

// The modes of a write, as Set Deaths' set, add and subtract (subtract stops at 0).
enum EudMode { EUD_SET = 0, EUD_ADD = 1, EUD_SUB = -1 };

// The map's triggers as they load. EudMapBegin as a map is read (PlayersMapBegin),
// EudMapTrigger for each 0x960-byte record of TRIG and EudMapTriggersRead after them
// (maphdr_TRIG), which makes a new game on an EUD map an EUD game.
void EudMapBegin();
void EudMapTrigger(const BYTE *pTrigger);
void EudMapTriggersRead();
// The map last read: bit 0 a Deaths condition reaches memory, bit 1 a Set Deaths action.
unsigned EudMapKind();

// As a game starts (game_init): no access logged yet and the triggers running.
void EudGameStart();
// An image on a frame its group does not have, not drawn (CSprite.cpp): logged once for each
// image in an EUD game.
void EudFrameMissing(WORD wImageID, unsigned nFrame, unsigned nFrames);
// An EUD game on a map with a Set Deaths action that reaches memory: Save Game is greyed out
// (CanSaveGame), as StarCraft: Remastered greys it, since a save does not hold what the map has
// written outside the game's own data, its pools grown and its command cards among it.
bool EudSavesBlocked();
// The game plays as an EUD game.
static inline bool EudGame() {
  return (g_hxRuleOptions & HX_OPT_EUD) != 0;
}
// An access no item serves has stopped the triggers (victory.cpp's loop, trigger_parse and
// trigger_execute).
extern bool g_eudHalted;
static inline bool EudHalted() {
  return g_eudHalted;
}

// The Deaths condition and the Set Deaths action of an EUD game set the mask they carry while
// their groups are read or set (cond_deaths, act_set_deaths): `SC` in a condition's or an
// action's last two bytes makes its location field the mask of the bits it reads or writes.
void EudMaskCondition(const void *pCondition);
void EudMaskAction(const void *pAction);
void EudMaskClear();

// One player's count of one unit type in an EUD game, a group already expanded: the counts
// 229 to 232 as 1.17 keeps them, the rest at 0x58A364 + 4 * (player + 12 * unit) through the
// address map (death_count).
DWORD EudDeaths(DWORD nPlayer, WORD wUnit);
// Set Deaths' set, add and subtract for one player (set_deaths, add_deaths, sub_deaths).
int EudSetDeaths(DWORD nPlayer, WORD wUnit, DWORD dwValue, EudMode mode);

// A dword at a 1.16.1 address, aligned or not, with the mask of the condition or action that
// runs. False, logged, when no item serves the address.
bool EudRead(DWORD dwAddr, DWORD *pValue);
bool EudWrite(DWORD dwAddr, DWORD dwValue, EudMode mode);

// An EUD game's triggers run from 1.16.1's eight lists, built from the game's own at its first
// trigger pass (victory.cpp): EudTriggersBegin with the count of nodes, EudTriggerAdd for each
// list's nodes in order, with the 0x960 bytes past a node's link, then EudTriggersEnd. A map's
// program links its own triggers into them, in the string section too.
struct TRIGGERNODE;
bool EudTriggersBuilt();
void EudTriggersBegin(DWORD nNodes);
void EudTriggerAdd(DWORD nList, const void *pRecord);
void EudTriggersEnd();
// The walk of a list by 1.16.1's addresses: its first node, the node at an address as a
// TRIGGERNODE to run in place, and the address after it, read once it has run. An address
// not served gives NULL or 0 and is logged.
DWORD EudTriggersFirst(DWORD nList);
TRIGGERNODE *EudTriggerAt(DWORD dwAddr);
DWORD EudTriggerNext(DWORD dwAddr);
// A list emptied, as 1.16.1 frees a player's when the player leaves (victory_quit,
// victory_disconnect).
void EudTriggersClear(DWORD nList);
// The trigger pass has run (triggers_loop): what the map wrote that Remastered applies once its
// triggers have run, a pool's free list built again and the buttons of its own a card shows.
void EudTriggersDone();

#endif
