// Priority.h -- declarations for src117/lang/Priority.cpp, the sprite
// draw-priority family (0x47ED50 to 0x47EF20). Free functions are __fastcall
// through /Gr and are declared without the keyword.
#ifndef OC_PRIORITY_H
#define OC_PRIORITY_H

#include "platform.h"

class CSprite;

// 0x47ED50 (95 B). Nullary void: priority_add reaches it with nothing set up
// and ignores EAX.
void priority_sort();

// 0x47EDB0 (31 B). Nullary void: zeroes sguiTop and the 256-byte
// sgbPriorityVisHash_g row map.
void priority_init();

// 0x47EEC0 (84 B). One argument in ECX, a CSprite*: both call sites in
// CSprite::PriorityAddRange load it straight out of the y-axis row walk. Void:
// the caller ignores EAX.
void priority_add(CSprite *pSprite);

// 0x47EDD0 (225 B). Nullary; CSprite::DrawAll uses the returned CSprite* as its
// loop cursor.
CSprite *priority_iter_begin();

// 0x47EF20 (24 B). Nullary; pops the next CSprite* off the iterator stack
// (sguiIndex/sgSprPriorityIterator), NULL when exhausted. CSprite::DrawAll
// dereferences the EAX right after.
CSprite *priority_iter_next();

#endif // OC_PRIORITY_H
