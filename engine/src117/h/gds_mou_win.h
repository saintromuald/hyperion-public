// gds_mou_win.h -- declarations for src117/lang/gds_mou_win.cpp, the Win32
// mouse: position, and the cursor cage. Free functions are __fastcall through
// /Gr and are declared without the keyword.

#ifndef OC117_GDS_MOU_WIN_H
#define OC117_GDS_MOU_WIN_H

#include "platform.h"

// 0x4CE820 sub_4CE820 (48 B), plain `ret`. ECX only, a flag that is stored
// whole in dword_6D3160 and tested for zero; both callers discard EAX.
void sub_4CE820(int bClip);

// 0x4CE890 mou_get_pos (38 B), plain `ret`. Two `int *` out-parameters in
// ECX/EDX, each written with a 32-bit store from GetCursorPos's POINT. The one
// caller discards EAX.
void mou_get_pos(int *pnX, int *pnY);

// 0x4CE8C0 mou_set_pos (31 B), plain `ret`. Both registers are short: the body
// widens them with movsx before SetCursorPos. All three callers pass literals.
void mou_set_pos(short x, short y);

// 0x4CE8E0 UpdateScreenRect (93 B). Nullary, plain `ret`; both callers discard
// EAX.
void UpdateScreenRect(void);

#endif  // OC117_GDS_MOU_WIN_H
