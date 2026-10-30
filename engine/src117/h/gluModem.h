// gluModem.h -- declarations for src117/lang/gluModem.cpp: the modem screens, with the
// phone-number entry and its phonebook, the device list, and the dial-status dialog.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUMODEM_H
#define OC117_GLUMODEM_H

// <stddef.h> comes before dialog.h on purpose: dialog.h's static_asserts use offsetof
// and it does not include <stddef.h> itself.
#include <stddef.h>
#include "platform.h"
#include "dialog.h"
#include "rect16.h"
#include "TSList.h"

// Each address below gives one body's oracle address and size; the declarations the TU
// uses are gathered at the bottom of the file.

// 0x45E280 do_atzone_dlg (120 B) -- the return is not void: the body ends
// `call dlg_run_modal / cmp eax, 3h / jnz / xor eax, eax / ret`, and a void return is what
// deletes that `xor eax, eax` as dead. enum_devices consumes this call and
// modemlist_get_device's in one shared `test eax, eax / jz / mov ecx, eax`, and `int` is
// byte-identical to the `_DEVICE *` the value really is.
intptr_t do_atzone_dlg();

// 0x45E300 do_modem_dlg (117 B)

// 0x45E380 (182 B)
BOOL enum_devices(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3);

// 0x45E440 glumodem_dlg_activate (188 B)

// 0x45E500 glumodem_phone_entry_activate (132 B)

// 0x45E590 sub_45E590 (207 B)

// 0x45E660 glumodem_phone_entry_deactivate (66 B)

// 0x45E6B0 glumodem_create_device (184 B) -- Reached only by address, so the argument
// list is a lower bound. __stdcall: the body reads no register input and pops its
// arguments, which under /Gr is byte-identical to a fastcall that ignores its two
// register arguments.

// 0x45E770 sub_45E770 (58 B) -- Reached only by address, so the argument list is a
// lower bound. EDX is a pointer whose class the bytes do not name.

// 0x45E7B0 glumodem_dlg_add_phonebook_entry (919 B)

// 0x45EB50 glumodem_phone_entry_dlg_fcn (98 B) -- Reached only by address, so the
// argument list is a lower bound.

// 0x45EBE0 glumodem_phone_entry_init (118 B)

// 0x45EC60 sub_45EC60 (72 B) -- ECX is only forwarded to a callee, so its width comes
// from that callee.

// 0x45ECB0 sub_45ECB0 (67 B)

// 0x45ED00 glumodem_dlg_delete_phonebook_entry (113 B)

// 0x45ED80 glumodem_dlg_fcn (318 B) -- Reached only by address, so the argument list
// is a lower bound.

// 0x45EF00 glumodem_enum_games_fn (57 B) -- Reached only by address, so the argument
// list is a lower bound. The bytes do not name what arg0 points at. __stdcall: the
// body reads no register input and pops its arguments, which under /Gr is
// byte-identical to a fastcall that ignores its two register arguments.

// 0x45EF40 glumodem_init_device (96 B) -- The bytes do not name what arg1 points at.

// 0x45EFA0 glumodem_join_fn (74 B) -- Reached only by address, so the argument list
// is a lower bound. __stdcall: the body reads no register input and pops its
// arguments, which under /Gr is byte-identical to a fastcall that ignores its two
// register arguments.

// 0x45EFF0 sub_45EFF0 (582 B)

// 0x45F240 sub_45F240 (60 B) -- Reached only by address, so the argument list is a
// lower bound.

// 0x45F280 modemlist_deactivate (394 B) -- ECX is only forwarded to a callee, so its
// width comes from that callee.

// 0x45F410 modemlist_dlg_activate (64 B) -- ECX is only forwarded to a callee, so its
// width comes from that callee.

// 0x45F450 modemlist_dlg_answer (450 B)

// 0x45F620 modemlist_dlg_fcn (137 B) -- Reached only by address, so the argument list
// is a lower bound.

// 0x45F6D0 modemlist_dlg_init (133 B)

// 0x45F760 modemlist_get_device (120 B)

// 0x45F7E0 modem_status_fn (57 B) -- Reached only by address, so the argument list is
// a lower bound. The body never reads arg3, so only its 4-byte footprint is known.
// __stdcall: the body reads no register input and pops its arguments, which under /Gr
// is byte-identical to a fastcall that ignores its two register arguments.

// 0x45F820 modem_status_dlg_activate (135 B)

// 0x45F8B0 modem_status_dlg_fcn (123 B) -- Reached only by address, so the argument
// list is a lower bound.

// 0x45F930 sub_45F930 (86 B) -- Reached only by address, so the argument list is a
// lower bound. EDX carries an argument the body never reads, so `int` is a
// placeholder. The body never reads arg0, so only its 4-byte footprint is known.

// 0x45F990 sub_45F990 (216 B)

// The four Storm calls this TU makes reach their import through an IAT thunk --
// SNetEnumDevices at 0x4D01CC, SNetEnumGamesEx at 0x4D01D2, SNetInitializeDevice at
// 0x4D01D8, SNetJoinGame at 0x4D01DE -- so none of them is declared here; the imports are
// declared in src117/h/Storm.h.

// `_DEVICE`, `_PHONENUMBER` and `modemprog_t` are defined in gluModem.cpp, with their
// field offsets and the sizes proven at their SMemAlloc sites (_DEVICE 0x10C,
// _PHONENUMBER 0x54). Three declarations below take them by pointer, which needs no more
// than an incomplete type, so the definitions stay where they were derived.
struct _DEVICE;
struct _PHONENUMBER;
struct modemprog_t;

// The two lists, spelled as the ledger spells their Clear rows. Only pointers to the node
// types appear, so the forward declarations above are enough.
#pragma pack(push, 1)
template <> struct TSList<_DEVICE,TSGetLink<_DEVICE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_DEVICE> link;
  // 0x45E1C0 (84 B). Unlinks and frees every node.
  void Clear(void);
};
template <> struct TSList<_PHONENUMBER,TSGetLink<_PHONENUMBER> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_PHONENUMBER> link;
  // 0x45E220 (84 B). Unlinks and frees every node.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<_DEVICE,TSGetLink<_DEVICE> > DEVICELIST;
typedef TSList<_PHONENUMBER,TSGetLink<_PHONENUMBER> > PHONEBOOK;
OC_X86_LAYOUT(sizeof(DEVICELIST) == 12, "TSList.h layout");
static_assert(offsetof(DEVICELIST, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(DEVICELIST, link) == 0x04, "Clear reads [this+8] as link.next");
OC_X86_LAYOUT(sizeof(PHONEBOOK) == 12, "TSList.h layout");
static_assert(offsetof(PHONEBOOK, _unmapped_00) == 0x00, "TSList.h layout");
static_assert(offsetof(PHONEBOOK, link) == 0x04, "Clear reads [this+8] as link.next");

// `struct lbox_t` is dlg_lbox.h's, forward-declared rather than included so this header
// takes on no dependency.
struct lbox_t;

// The dial-status callback pair. PFN_MODEM_DONE is __cdecl (its call sites push
// nothing and clean nothing); PFN_MODEM_STATUS is __stdcall, and
// modem_status_fn below IS one.
typedef void(__cdecl *PFN_MODEM_DONE)(void);

// The declarations themselves, with the pointees and widths their call sites prove.
void do_modem_dlg();
void glumodem_phone_entry_activate(ctrl_t *pDlg);
void glumodem_phone_entry_deactivate(ctrl_t *pDlg);
BOOL sub_45E770(ctrl_t *pCtrl, dlgevt_t *pEvent);
int glumodem_phone_entry_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
void glumodem_phone_entry_init(ctrl_t *pDlg);
void sub_45EC60(ctrl_t *pDlg);
void sub_45ECB0(ctrl_t *pDlg, lbox_t *pBox);
void glumodem_dlg_delete_phonebook_entry(ctrl_t *pDlg);
int glumodem_init_device(_DEVICE *pDevice, DWORD_PTR dwCaps, DWORD_PTR dwArg2,
                         const modemprog_t *pProgram);
void sub_45F240(ctrl_t *pDlg);
void modemlist_dlg_activate(ctrl_t *pDlg);
int modemlist_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
void modemlist_dlg_init(ctrl_t *pDlg);
intptr_t modemlist_get_device();
BOOL __stdcall modem_status_fn(const char *pszStatus, LONG nA, LONG nB,
                               int nUnused, PFN_MODEM_DONE pfnDone);
void modem_status_dlg_activate(ctrl_t *pDlg);
int modem_status_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// Two more rows whose placeholders their call sites named; both swaps are byte-neutral, a
// pointer for an int.
void sub_45E590(ctrl_t *pCtrl, dlgevt_t *pEvent);
int glumodem_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// Storm's per-game record handed to the SNetEnumGamesEx callback; defined in
// gluModem.cpp.
struct snetgame_t;
BOOL __stdcall glumodem_enum_games_fn(snetgame_t *pGame);
// A _beginthreadex routine; its argument is the game name or phone number to join.
ULONG __stdcall glumodem_join_fn(void *pszTarget);
// The SNetEnumDevices callback: one call per device, appended to the list.
BOOL __stdcall glumodem_create_device(DWORD dwId, const char *pszName,
                                      const char *pszDesc);
void sub_45F930(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void glumodem_dlg_activate(ctrl_t *pDlg);
void glumodem_dlg_add_phonebook_entry(ctrl_t *pDlg);
void modemlist_deactivate(ctrl_t *pDlg);
int modemlist_dlg_answer(ctrl_t *pDlg);
void sub_45EFF0(ctrl_t *pDlg);
// Also the 0x38 timer's callback; nTimerId is 0xFFFF from the two direct callers.
void sub_45F990(ctrl_t *pDlg, int nTimerId);

#endif  // OC117_GLUMODEM_H
