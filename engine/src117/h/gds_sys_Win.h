// gds_sys_Win.h -- declarations for src117/lang/gds_sys_Win.cpp, the Win32
// system helpers. Free functions are __fastcall through /Gr and are declared
// without the keyword.

#ifndef OC117_GDS_SYS_WIN_H
#define OC117_GDS_SYS_WIN_H

#include "platform.h"
#include <windows.h>   // HWND, HRESULT

// 0x4CEC50 (127 B). ECX is the window, passed to GetWindowRect and GetDC.
void center_window(HWND hWnd);

// 0x4CECD0 (194 B). ECX the code, EDX the buffer, one stack word its size
// (`ret 4h`); returns the buffer (`mov eax, esi`), which both callers use.
char *strGetError(DWORD dwError, char *pszBuf, int cchBuf);

// 0x4CEDA0 (841 B) and 0x4CF350 (394 B). Same register shape as strGetError;
// hr is compared signed. EAX is SStrCopy's leftover and the caller discards it.
void get_ddraw_error(HRESULT hr, char *pszBuf, int cchBuf);
void get_dsound_error(HRESULT hr, char *pszBuf, int cchBuf);

// 0x4CF4E0 (7 B). ECX is pushed to exit(); never returns.
void gds_exit(int nCode);

#endif  // OC117_GDS_SYS_WIN_H
