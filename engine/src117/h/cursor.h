// cursor.h -- declarations for src117/lang/cursor.cpp: which cursor shows, from
// the edge scroll, the minimap, the console mask and what is under the pointer.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_CURSOR_H
#define OC117_CURSOR_H

#include "platform.h"
#include "bmp.h"       // cursor_set_console_offset's parameter
#include "gds_mou_win.h"

class CUnit;

// DetermineCursor -- 0x43E6D0, 106 B. Nullary; returns the cursor id as a full
// dword.
int DetermineCursor();

// is_enemy_unit -- 0x43E740, 40 B. The unit in ECX; TRUE when the local player
// is not allied to its owner and no replay is playing. Returns exactly 0 or 1.
BOOL is_enemy_unit(CUnit *pUnit);

// cursor_destroy -- 0x43E770, 46 B. Frees each of the 19 cursor slots and zeroes
// it. Nullary and void: cursor_init registers it with app_add_exit, whose
// callback takes nothing.
void cursor_destroy();

// cursor_get -- 0x43E7A0, 6 B. The current cursor id.
int cursor_get();

// cursor_get_scroll_cursor -- 0x43E7B0, 222 B. Both register arguments are int
// out-parameters, ECX the X axis and EDX the Y axis. Returns the scroll cursor's
// id, or 0 when the pointer is at no edge.
int cursor_get_scroll_cursor(int *pnScrollX, int *pnScrollY);

// console_collision -- 0x43E890, 83 B. ECX is x and EDX is y, both compared
// signed. Nonzero when the point is over the console.
int console_collision(int nX, int nY);

// cursor_init -- 0x43E8F0, 133 B. Nullary, void.
void cursor_init();

// cursor_is_locked -- 0x43E980, 6 B. A byte load: only AL is defined, so BYTE.
BYTE cursor_is_locked();

// cursor_lock / cursor_unlock -- 0x43E990 / 0x43EA30, 8 B each. Store 1 or 0
// into the lock byte at 0x665D68. Nullary, void.
void cursor_lock();
void cursor_unlock();

// cursor_set -- 0x43E9A0, 27 B. One int parameter, an index into the cursor
// pointer table; it tail-jumps cursor_set_image (cur.cpp). Void.
void cursor_set(int nCursorId);

// cursor_set_console_offset -- 0x43E9C0, 105 B. The console mask bitmap in ECX:
// status_load_console passes its 8-byte bmp_t at 0x6AB504.
void cursor_set_console_offset(const bmp_t *pMask);

// cursor_update -- 0x43EA40, 69 B. Nullary, void.
void cursor_update();
// Hyperion: a game begins; the unit kept under the cursor is the last game's (cursor.cpp)
void cursor_forget_target();

// DetermineTargetCursor -- 0x43EA90, 130 B. Nullary. Returns a cursor id, 0 to
// 8, which DetermineCursor's tail jump passes on as its own return.
int DetermineTargetCursor();

#endif  // OC117_CURSOR_H
