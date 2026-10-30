// gluError.h -- declarations for src117/lang/gluError.cpp, the error screens:
// the modal error dialogs the glue shell puts up, their event handler, and the
// image and binary loaders behind them
//
// Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default. Where a parameter is spelled `int` or `void *`, only its
// 4-byte footprint is derived; a call site that names one should.

#ifndef OC117_GLUERROR_H
#define OC117_GLUERROR_H

#include "platform.h"
#include "dialog.h"

// 0x456B50 sub_456B50 (459 B). Two register arguments, EDX a pointer, and no
// caller reads the EAX it inherits. The body touches x87 and SSE, so a
// floating-point return would not show in EAX either way.
void sub_456B50(intptr_t nArg0, void *pArg1);

// 0x456D20 err_dlg_fcn (116 B). The error dialog's event handler, address-taken
// three times: two register arguments, and the tail jmp to ctrl_dlg gives the
// int its one consuming caller reads whole.
int err_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x456DA0 err_dlg_init (315 B). One register argument, and no caller reads
// the EAX it inherits.
void err_dlg_init(intptr_t nArg0);

// 0x456EE0 err_load_img (112 B). The dialog loader's image callback: ECX is
// only forwarded to a callee, so this body shows no width for it, and the last
// two stack words are never read -- only their 4-byte footprint is derived.
void err_load_img(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3, int nArg4);

// 0x456F50 err_load_binary (81 B), the dialog loader's binary callback. Its
// address is taken three times with no direct call site, so the return comes
// from the typedef it is installed through: dlg_dlg.h's PFN_DLGLOADER returns
// ctrl_t * over exactly these five arguments.
ctrl_t *err_load_binary(void *pCtx, ULONG_PTR dwZero, ULONG_PTR dwZero2,
                        void *pA, dlg_dword_t dwB);

// 0x456FB0 do_err_ok_statement_dlg (285 B). The return is a BYTE, measured
// rather than argued: as BYTE the body is 285 B with no difference against the
// oracle, as void 270 B. Both parameters are strings.
BYTE do_err_ok_statement_dlg(const char *pszText, const char *pszStatement);

// 0x4570D0 sub_4570D0 (216 B): the one-string OK popup, entered only by
// do_err_ok_statement_dlg's tail jmp.
BYTE sub_4570D0(const char *pszText);

// 0x4571B0 do_err_okcancel_dlg (228 B) is a body of this TU, declared in
// src117/h/gluCmpgn.h, which gluError.cpp includes. All six of its consuming
// callers use AL only, so its return is a BYTE and not an int.

// 0x4572A0 sub_4572A0 (215 B). One register argument, and EAX is written on no
// path to the ret.
void sub_4572A0(intptr_t nArg0);

// 0x457380 deactivate_modal_popup (26 B) is likewise a body of this TU declared
// elsewhere, in src117/h/glues.h beside its sibling deactivate_popup. Both are
// nullary: each tail-jumps dlg_deactivate, and glue_panic_fallback_fn calls
// them with nothing in ECX or EDX.

// SBmpDecodeImage is a Storm import, not a function of this TU: the row at
// 0x4D01B4 is its six-byte `jmp dword ptr [0x4EB38C]` thunk. It is declared in
// src117/h/Storm.h at nine __stdcall arguments, ordinal 321, with the matching
// `_SBmpDecodeImage@36` line in src117/storm_imports.def.

#endif  // OC117_GLUERROR_H
