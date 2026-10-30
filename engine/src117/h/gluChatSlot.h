// gluChatSlot.h -- declarations for src117/lang/gluChatSlot.cpp: the chat room's player
// slots, and the slot entries src117/lang/net_chat_cmds.cpp reaches.
//
// Free functions are spelled without an explicit __fastcall, which /Gr already makes the
// default.

#ifndef OC117_GLUCHATSLOT_H
#define OC117_GLUCHATSLOT_H

// <stddef.h> comes before dialog.h on purpose: dialog.h's static_asserts use offsetof
// and it does not include <stddef.h> itself.
#include <stddef.h>
#include "platform.h"
#include "dialog.h"
#include "rect16.h"

// `struct lbox_t` is dlg_lbox.h's, forward-declared rather than included so this header
// adds no dependency.
struct lbox_t;

// 0x451780 (11 B) -- nullary; recv_net_cmd_make_public tail-jumps to it.
void sub_451780();

// 0x4517D0 (71 B) -- ECX and EDX both read as 8 bits. BOOL rather than int: every `ret`
// materialises 0 or 1 and the one consuming caller uses the full EAX.
BOOL sub_4517D0(BYTE bArg0, BYTE bArg1);

// 0x4523A0 (25 B) -- `movzx eax, dl` pins EDX at 8 bits; `[ecx*4+6689F4h]` pins ECX as a
// 32-bit index, since the scale is 4. One arm tail-jumps to sub_453CC0, the other returns
// with the zero-extended EDX in EAX, which nothing consumes.
void sub_4523A0(int nIndex, BYTE bValue);

// 0x451820 (11 B) -- the body reads neither register, but its one caller,
// recv_net_cmd_change_slot, loads `mov cl, [edi+1h]` and `mov dl, 5|6|8` for it: two BYTE
// parameters. BOOL: every `ret` materialises 0 or 1 and the one caller uses the full EAX.
BOOL sub_451820(BYTE bSlot, BYTE bType);

// 0x451830 (85 B) -- Reached only by address, so the argument list is a lower bound.
void sub_451830(intptr_t nArg0, int nArg1);

// 0x451890 (57 B) -- ECX carries an argument the body never reads, so `int` is a
// placeholder.
void sub_451890(intptr_t nArg0);

// 0x4518D0 (98 B)
void sub_4518D0(intptr_t nArg0);

// 0x451940 (184 B)
int sub_451940(int nArg0);

// 0x451A00 (162 B)
int CountSides();

// 0x451AB0 (21 B)
void sub_451AB0();

// 0x451AD0 (118 B)
void sub_451AD0(intptr_t nArg0);

// 0x451B50 (535 B) -- ECX carries an argument the body never reads, so `int` is a
// placeholder.
void sub_451B50(intptr_t nArg0, int nArg1);

// 0x451DA0 (158 B)
void sub_451DA0(intptr_t nArg0);

// 0x451E40 (595 B) -- ECX carries an argument the body never reads, so `int` is a
// placeholder.
void sub_451E40(intptr_t nArg0, int nArg1);

// 0x4520C0 (670 B)
void sub_4520C0(intptr_t nArg0);

// 0x452360 (51 B) -- ECX carries an argument the body never reads, so `int` is a
// placeholder.
void sub_452360(intptr_t nArg0);

// 0x4523C0 (119 B)
void sub_4523C0(int nArg0);

// 0x452440 (409 B) -- The bytes do not name what arg0, arg1 and arg2 point at.
void sub_452440(intptr_t nArg0, int nState, int nPlayer, int *pbChanged, int *pbRedraw);

// 0x452600 (23 B) -- ECX is a pointer whose class the bytes do not name.
// noinline: its one caller, sub_452AB0, calls it where MSVC would splice it in.
__declspec(noinline) void sub_452600(void *pArg0);

// 0x452620 (171 B) -- Reached only by address, so the argument list is a lower bound.
// ECX is a pointer whose class the bytes do not name. EDX carries an argument the
// body never reads, so `int` is a placeholder. The body never reads arg0 and arg1, so
// only their 4-byte footprints are known.
void sub_452620(void *pArg0, int nArg1, int nArg2, int nArg3);

// 0x4526D0 (684 B) -- EDX is a pointer whose class the bytes do not name.
char *sub_4526D0(int nIdx, int nStrOfs);

// 0x4529A0 (83 B)
void sub_4529A0(int nArg0, int nArg1);

// 0x452A00 (163 B) -- Reached only by address, so the argument list is a lower bound.
void sub_452A00();

// 0x452AB0 (299 B) -- Reached only by address, so the argument list is a lower bound.
void sub_452AB0();

// 0x452BE0 (378 B) -- The body writes arg0's slot as scratch, which is not evidence
// of a reference.
void sub_452BE0(intptr_t nArg0, int nArg1, int nArg2, BYTE bArg3);

// 0x452D60 (516 B) -- The bytes do not name what arg2 points at. The body writes
// arg0's slot as scratch, which is not evidence of a reference.
// nArg3 is `int` despite the one-byte read: declared BYTE, its dead slot takes a spill
// the oracle puts in nArg4's.
void sub_452D60(intptr_t nArg0, int nArg1, int nArg2, int nArg3, int nArg4, int nArg5);

// 0x452F70 (583 B)
void sub_452F70(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x4531C0 (183 B) -- The body never reads arg0, so only its 4-byte footprint is
// known.
void sub_4531C0(intptr_t nArg0, int nArg1, int nArg2, int nArg3);

// 0x453280 (38 B) -- Reached only by address, so the argument list is a lower bound.
// ECX and EDX are pointers whose class the bytes do not name.
int sub_453280(void *pArg0, void *pArg1);

// 0x4532B0 (1178 B)
void sub_4532B0(intptr_t nArg0, intptr_t nArg1, int nArg2, int nArg3, BYTE bArg4, int nArg5);

// 0x453770 (74 B) -- Reached only by address, so the argument list is a lower bound.
// ECX and EDX are pointers whose class the bytes do not name.
int sub_453770(void *pArg0, void *pArg1);

// 0x4537C0 (220 B) -- ECX is the listbox, dereferenced at +0x26 and passed on to
// lbox_get_data unchanged. nItem is a byte, which this body shows nothing of: EDX is
// forwarded straight into `call lbox_get_data`, declared `void *lbox_get_data(lbox_t *,
// unsigned char)` in dlg_lbox.h, and the sole caller sub_453770 hands the listbox
// selection over with `mov dl, al`.
BOOL sub_4537C0(lbox_t *pBox, unsigned char nItem);

// 0x4538D0 (194 B)
void sub_4538D0(intptr_t nArg0, BYTE bArg1);

// 0x4539A0 (86 B) -- Reached only by address, so the argument list is a lower bound.
// The PFN_CTRL_EVT shape: `cmp word ptr [edx+0Ch], 0Eh` and
// `mov eax, dword ptr [edx]` make EDX the event, `mov esi, ecx` makes ECX the control.
int sub_4539A0(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x453A00 (78 B) -- ECX is the control, dereferenced at +0x26. bRace is 8 bits, not an
// int: the body opens `mov bl, dl`, never touches the argument wider and stores it back
// with `mov byte ptr [esi+70h], bl`, and its caller sub_4539A0 passes it with
// `mov dl, al`, where an `int` parameter would force `movzx edx, al`.
void sub_453A00(ctrl_t *pCtrl, BYTE bRace);

// 0x453A50 (129 B)
void sub_453A50(intptr_t nArg0, intptr_t nArg1);

// 0x453AE0 (42 B) -- Reached only by address, so the argument list is a lower bound.
// ECX is a pointer whose class the bytes do not name. EDX carries an argument the
// body never reads, so `int` is a placeholder. The body never reads arg0 and arg1, so
// only their 4-byte footprints are known.
void sub_453AE0(void *pArg0, int nArg1, int nArg2, int nArg3);

// 0x453B10 (52 B) -- ECX is a pointer whose class the bytes do not name.
void sub_453B10(void *pArg0);

// 0x453B50 (103 B) -- Reached only by address, so the argument list is a lower bound.
// EDX carries an argument the body never reads, so `int` is a placeholder. The body
// never reads arg0, so only its 4-byte footprint is known.
void sub_453B50(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3);

// 0x453BC0 (182 B) -- Reached only by address, so the argument list is a lower bound.
// ECX and EDX are pointers whose class the bytes do not name.
int sub_453BC0(void *pArg0, void *pArg1);

// 0x453C80 (59 B) -- ECX is a pointer whose class the bytes do not name.
void sub_453C80(void *pArg0, int nArg1);

// 0x453CC0 (201 B)
void sub_453CC0();

// SStrCmp is not declared here: 0x4D019C is a Storm IAT thunk,
// `jmp dword ptr [0x4EB37C]`, and the import itself is declared in src117/h/Storm.h.

#endif  // OC117_GLUCHATSLOT_H
