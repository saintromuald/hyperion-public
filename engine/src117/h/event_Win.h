#ifndef OC117_EVENT_WIN_H
#define OC117_EVENT_WIN_H

// event_Win.h -- declarations for src117/lang/event_Win.cpp, the Win32 event
// layer (0x43F100 - 0x4405C0): the accelerator-table swap family, the window
// and message pump, the key-modifier reader, the screen capture and the
// problem-app sweep.
//
// The Win32 and IMM32 imports are declared in the .cpp, not here: a header that
// held them would collide with <windows.h>'s prototypes in a TU including both.
// Handles are `void *`, so no header needs <windows.h>.

#include "platform.h"

// Hyperion: a window message's lParam and a window procedure's result, LPARAM
// and LRESULT in <windows.h>. 1.17 spelled them `long`, which they are on x86 and
// which keeps WndProc's decorated name; on x64 they are pointer-wide, and a `long`
// cuts WM_NCCREATE's CREATESTRUCT pointer to 32 bits.
#if defined(_WIN64) || defined(__LP64__)
typedef __int64 win_lparam_t;
#else
typedef LONG win_lparam_t;
#endif

// The accelerator handler slot at 0x665E30. accel_set_single and
// accel_set_multi store dialog_accel in it, accel_set_sound and init_events
// game_mute_key_hook. ECX is the event record, and neither returns a value.
struct dlgevt_t;

typedef void (*PFN_ACCELPROC)(dlgevt_t *pEvent);

void dialog_accel(dlgevt_t *pEvent);        // 0x4487A0, game_evt.cpp
void game_mute_key_hook(dlgevt_t *pEvent);  // 0x448F20, game_evt.cpp

// Callees in other TUs that no other header of ours declares.
void CloseFlic();                    // 0x47EC20, playflic.cpp
void game_reset_handlers();          // 0x448DD0, game_evt.cpp

void *GetAcceleratorTable(int nResId);   // 0x4CB810, gds_local.cpp
void cur_update();                       // 0x43E610, cur.cpp
void CheckAnyAsyncFailed();              // 0x444470, gamedata.cpp
void music_update();                     // 0x46EE70, music.cpp
void timer_update();                     // 0x4C5A20, dlg_timer.cpp

// Storm's ordinal 342 (thunk 0x4D0082), linked by its undecorated name through
// storm_imports.def, hence the extern "C". Two stack words.
extern "C" BOOL __stdcall SDrawCaptureScreen(const char *pszFile, int nFlags);  // 0x4D0082

// The bodies, in VA order.

// The accelerator swap family (0x43F100 - 0x43F210). accel_set_sound,
// accel_restore and accel_save are declared in TrigAct.h.
void accel_set_multi();              // 0x43F120
void accel_set_single();             // 0x43F140
void accel_close();                  // 0x43F1A0
void accel_init();                   // 0x43F210
// Hyperion: the tables again with the profile's keys (the hotkeys milestone K2)
void accel_apply_profile();

// The event layer proper.
void init_events();                  // 0x43F330
void show_cursor(int bShow);         // 0x43F380
void init_lang_imm(int bEnable);     // 0x43F3F0
void WinEvtCaptureChanged();         // 0x43F450
// The three Win-event bodies take the mouse-button flag mask ORed into
// gwMouseFlags_l (unread in WinEvtMouseWheel), the event's wTag, the default
// handler, a PFN_WINEVT called when no dialog takes the event, and the packed
// coordinate, a Win32 LPARAM. The parameter names here predate that reading;
// the definitions name them right.
void WinEvtButtonDown(short sX, WORD wY, intptr_t nUnused, int nFlags);           // 0x43F480
void WinEvtButtonUp(int nX, WORD wY, intptr_t nUnused, int nFlags);               // 0x43F520
void WinEvtMouseWheel(int nUnusedEcx, WORD wDelta, intptr_t nUnused, int nFlags); // 0x43F5B0

void close_window();                 // 0x440020
// RunMsgDispatcher (0x440050) is declared in dlg_dlg.h, and RunEvtDispatcher
// (0x4401C0) in save_dlg.h.

// get_key_modifiers returns an int: 1.17's `mov eax, esi` at the ret is a
// 32-bit return, though its callers keep 16 bits.
int get_key_modifiers();             // 0x440060
void SendMouseMovedEvt();            // 0x4400D0
int GetNextMessage(intptr_t nArg0, int nArg1);   // 0x440130
void screen_capture();               // 0x440310

void kill_problem_apps();                   // 0x4405C0

// 0x43F600 (2112 B). The window procedure, `ret 10h` with four stack words;
// InitWindow takes its address. It uses these SDK-free types, and the call site
// casts.
win_lparam_t __stdcall WndProc(void *hWnd, unsigned int uMsg, uintptr_t wParam,
                       win_lparam_t lParam);

#endif  // OC117_EVENT_WIN_H
