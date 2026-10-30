// game_evt.h -- declarations for src117/lang/game_evt.cpp: the in-game map's
// input event handlers and the table that installs them. Free functions are
// __fastcall through /Gr and are declared without the keyword.
//
// Four of this TU's functions are declared in other headers and are not
// repeated here: dialog_accel, game_mute_key_hook and game_reset_handlers in
// event_Win.h, game_hold_cursor in minimap.h.

#ifndef OC117_GAME_EVT_H
#define OC117_GAME_EVT_H

#include "platform.h"

struct dlgevt_t;

// The hook family. Each receives the 0x14-byte event record in ECX, which only
// the dispatch sites show (a body that ignores it is the same code as a nullary
// one): game_lmouse_down_hook and game_rmouse_down_hook each `mov ecx, esi`
// before the indirect jmp, and SendMouseMovedEvt does the same for
// gEventHooks[] (usel.cpp).

// 0x448760 evt_placebox_off (21 B). Nullary; no caller reads EAX.
// game_reset_handlers casts it into sgfnGameMapRMouseDown, whose slot type is
// one argument wider.
void evt_placebox_off();

// 0x448780 game_keychar_hook (32 B). ECX is dereferenced at +8 as a WORD, the
// keyboard arm of dlgevt_t that dialog.h leaves unnamed.
void game_keychar_hook(dlgevt_t *pEvent);

// 0x448C20 game_event_reset (46 B). ECX is read 32-bit and only ever tested
// for zero; its four callers all set it and none reads EAX.
void game_event_reset(int nSquelchVoice);

// 0x448C90 game_lmouse_down_hook (24 B).
void game_lmouse_down_hook(dlgevt_t *pEvent);

// 0x448CB0 game_mmouse_down_hook (100 B). Reads pEvent->nX / nY as signed
// words at +0x0E / +0x10.
void game_mmouse_down_hook(dlgevt_t *pEvent);

// 0x448D20 game_mmouse_moved_hook (148 B). Takes its position from
// gCursorPos_g, not from the event.
void game_mmouse_moved_hook(dlgevt_t *pEvent);

// 0x448DC0 game_mmouse_up_hook (16 B).
void game_mmouse_up_hook(dlgevt_t *pEvent);

// 0x448EB0 game_rmouse_down_hook (86 B).
void game_rmouse_down_hook(dlgevt_t *pEvent);

// 0x448F10 game_timer (5 B). A thunk to gamesnd_update, which is nullary.
void game_timer(dlgevt_t *pEvent);

// Callees in other TUs that no other header declares.

// 0x446050 screen_rclick, gamemap.cpp. Its own body never reads ECX; only
// game_rmouse_down_hook's `mov ecx, esi` in front of the tail jmp gives it a
// parameter.
void screen_rclick(dlgevt_t *pEvent);

// 0x445D70 gamemap_lclick_target, gamemap.cpp. Reads ECX, tail jmps
// game_hold_cursor.
void gamemap_lclick_target(dlgevt_t *pEvent);

// 0x463EE0 game_key_down_num. ECX carries the digit game_keychar_hook peels
// off the key code; its one caller discards EAX. A BYTE, not an int: as an int,
// sub_463ED0's tail jump to it widens with `movzx ecx, bl` where 1.17 has
// `mov cl, bl`.
void game_key_down_num(BYTE nDigit);

#endif  // OC117_GAME_EVT_H
