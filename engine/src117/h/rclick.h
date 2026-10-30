// rclick.h -- declarations for src117/lang/rclick.cpp: the right-click order
// query, which turns a click on a target into the order id to issue. The free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_RCLICK_H
#define OC117_RCLICK_H

#include "platform.h"
#include "CUnit.h"

// The rclick_query_* family shares one signature. They are reached only through
// the query_click table; sub_47F530 calls them with the unit in ECX, x in EDX,
// then y, &a CUnit * local and the unit type 0xE4 on the stack. The return is a
// full 32-bit order id: rclick_query_worker compares rclick_query_harvester's
// result with 0xBD.

// 0x47EF40. Returns 0x17 and reads nothing.
int rclick_query_none(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47EF50. The general attack/move query.
int rclick_query_attacker(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47F010. The query for a tower: an enemy target that is neither a circle of
// power nor flagged 0x800 gets order 8, otherwise the clicking unit's
// gubUnitOrderGuard row.
int rclick_query_tower(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47F070. The query for anything that can move or rally.
int rclick_query_mover(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47F180. The SCV/drone/probe query: harvest first, then repair, transport
// and the general attack query.
int rclick_query_scv(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47F2C0. Harvest first, then the general attack query.
int rclick_query_worker(CUnit *pUnit, int nX, int nY, CUnit **ppTarget, WORD wUnitType);

// 0x47F4B0. Whether this unit can act on a right click at all.
BOOL unit_has_rclick_action(CUnit *pUnit);

// 0x47F9D0. Walks the selection and plays the ack sound and error string for
// the click. ECX is the clicked unit.
BOOL rclick_interpret(CUnit *pTarget);

// 0x47FC60. The harvest arm, shared by rclick_query_worker and
// rclick_query_scv. It takes the target already dereferenced, has no unit-type
// argument, and reads neither nX nor nY.
int rclick_query_harvester(CUnit *pUnit, int nX, int nY, CUnit *pTarget);

struct dlgevt_t;

// 0x47F300. The game map's default right click, installed as
// sgfnGameMapRMouseDown by game_evt.cpp; ECX is the event.
void __fastcall gamemap_rclick_default(dlgevt_t *pEvent);

// 0x47F8D0. The minimap right click; ECX is the event, whose cursor pair it reads.
void __fastcall rclick_action(dlgevt_t *pEvent);

// Flashes the target marker at a clicked map position. The third argument is
// the clicked unit: the body dereferences its +0x0C sprite.
class CThingy;
CThingy *show_target_x(int x, int y, CUnit *pTarget);

#endif  // OC117_RCLICK_H
