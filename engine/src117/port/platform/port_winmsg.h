// port_winmsg.h -- the game window's messages, made from SDL's events
// (docs/modules/platform.md, "The window and the messages").
//
// Hyperion keeps 1.17's window procedure (lang/event_Win.cpp, WndProc) and its pumps as they
// are, but the window is SDL's on every platform, and the messages come from this layer: SDL's
// events turned into the messages Windows would have sent, the messages the game posts itself,
// and its timers. The functions are the Windows calls the game makes under names of their own,
// since on Windows an engine function cannot take the place of one the game imports from user32.
// A message carries what Windows puts in it (a key's virtual-key code and lParam bits, a mouse
// message's client position and button flags, the wheel in 120ths), which the game and
// Hyperion's hooks in WndProc read.
#pragma once
#include "../../h/platform.h"

// Windows' MSG: the game's win_msg_t (lang/event_Win.cpp) and the front end's.
struct WinMsg {
  void *hWnd;
  unsigned int uMsg;
  UINT_PTR wParam;
  LONG_PTR lParam;
  DWORD dwTime;
  LONG nPtX;
  LONG nPtY;
#if defined(_WIN64) || defined(__LP64__)
  DWORD dwPrivate;  // MSG's lPrivate; at 32 bits the struct ends at the point
#endif
};

typedef LONG_PTR (__stdcall *WinMsgProc)(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam);
typedef void (__stdcall *WinMsgTimerProc)(void *hWnd, unsigned int uMsg, UINT_PTR idEvent, DWORD dwTime);

// The window: SDL's, created hidden, with the game's procedure. On Windows the handle is
// the window's HWND, of the class SWarClass as 1.17's was; elsewhere it is SDL's window.
void *WinMsgCreateWindow(WinMsgProc pfnProc, const char *pszTitle);
struct SDL_Window *WinMsgSdlWindow(void);
void *WinMsgWindowHandle(void);

// The pump: PeekMessage(PM_REMOVE) and GetMessage, for any window and any message.
BOOL WinMsgPeek(WinMsg *pMsg);
BOOL WinMsgGet(WinMsg *pMsg);
LONG_PTR WinMsgDispatch(const WinMsg *pMsg);
BOOL WinMsgTranslate(const WinMsg *pMsg);
BOOL WinMsgPost(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam);
LONG_PTR WinMsgSend(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam);
LONG_PTR WinMsgDefProc(void *hWnd, unsigned int uMsg, UINT_PTR wParam, LONG_PTR lParam);

// Timers: SetTimer and KillTimer. A timer with a procedure calls it from the pump.
UINT_PTR WinMsgSetTimer(void *hWnd, UINT_PTR idEvent, unsigned int nMs, WinMsgTimerProc pfnProc);
BOOL WinMsgKillTimer(void *hWnd, UINT_PTR idEvent);

// The keyboard and mouse: GetKeyState and GetAsyncKeyState (a key's or a button's VK
// code), SetCapture and ReleaseCapture, ShowCursor.
short WinMsgKeyState(int nVirtKey);
// Hyperion's menus are drawing: what is typed for the next quarter second comes as UTF-8, every
// character, where the game's boxes take Windows-1252's as one byte.
void WinMsgTypeUtf8(void);
// A streamed seat's browser (port_lanstream.cpp): a key down or up, with the character it types
// (Windows-1252, 0 for none), and the mouse at a game pixel (WM_MOUSEMOVE, a button's down, up or
// double click), as the window's own would come.
void WinMsgRemoteKey(int vk, bool bDown, unsigned int uChar);
void WinMsgRemoteMouse(unsigned int uMsg, int nX, int nY);
short WinMsgAsyncKeyState(int nVirtKey);
#if defined(__EMSCRIPTEN__)
// A browser's page holds the pointer lock on the canvas in full screen (web/play): the mouse
// moves by its own amounts then, and SDL keeps its position inside the window, but not while a
// button is held, when SDL's imaginary capture lets it run past. A position past the window's
// edge comes back to it, SDL's own too, as a pointer ClipCursor holds would stop there.
void WinMsgWebHeld(float *pX, float *pY);
#endif
void *WinMsgSetCapture(void *hWnd);
BOOL WinMsgReleaseCapture(void);
int WinMsgShowCursor(BOOL bShow);

// Accelerator tables: an array of Windows' ACCEL (6 bytes: fVirt, key, cmd), loaded from a
// module's RT_ACCELERATOR resource or made from an array, and TranslateAccelerator, which
// turns a matching key into WM_COMMAND.
void *WinMsgLoadAccelerators(void *hModule, int nResId);
// The same from an RT_ACCELERATOR resource's bytes (Hyperion's copy of 1.17's exe's).
void *WinMsgAccelerators(const void *pResource, unsigned long cb);
void *WinMsgCreateAccelerators(const void *pEntries, int nCount);
int WinMsgCopyAccelerators(void *hAccel, void *pEntries, int nCount);
BOOL WinMsgDestroyAccelerators(void *hAccel);
int WinMsgTranslateAccelerator(void *hWnd, void *hAccel, const WinMsg *pMsg);

// The milliseconds a message is stamped with: GetTickCount's.
DWORD WinMsgTicks(void);
