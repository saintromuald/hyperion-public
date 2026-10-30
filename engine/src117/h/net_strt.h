// net_strt.h -- declarations for src117/lang/net_strt.cpp, the multiplayer
// start countdown. The free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_NET_STRT_H
#define OC117_NET_STRT_H

#include "Storm.h"   // the real <windows.h>: HWND/UINT/UINT_PTR for the TIMERPROC
#include "platform.h"

// 0x47A140.
void net_strt_init();

// 0x47A1A0. A Win32 TIMERPROC, so __stdcall where /Gr would make it __fastcall.
// begin_countdown passes it to SetTimer; the body reads none of its arguments.
void __stdcall countdown_cb(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                            DWORD dwTime);

// 0x47A210. Returns 0 on the early path and 1 on the other, though both
// callers discard it.
BOOL end_countdown();

// 0x47A160. Arms the one-second timer and shows the first tick. The one caller
// sets ECX, which the body never reads.
void begin_countdown(int nArg0);

#endif  // OC117_NET_STRT_H
