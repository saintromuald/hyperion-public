// glues.h -- declarations for src117/lang/glues.cpp: the glue-screen shell every
// between-games menu screen sits on. Screen setup and teardown (glues_init,
// glues_clear_all, glue_screen_activate), the cursor, GRP, font and button initialisers,
// the transition fades, the string table, the CD-binary loader and the panic fallback.
//
// The screens themselves are not in this TU: do_main_dlg, do_login_dlg, do_join_dlg,
// do_load_dlg, do_modem_dlg, do_ready?_dlg and do_xcampaign_dlg live in gluMain.cpp,
// gluLogin.cpp, gluJoin.cpp, gluLoad.cpp, gluModem.cpp, gluRdy.cpp and gluXCmpgn.cpp.
//
// Member functions live in their class header; this file owns the TU's free functions.
// They are spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUES_H
#define OC117_GLUES_H

#include "platform.h"
#include "dlg_dlg.h"
#include "dlg_str.h"
#include "save_dlg.h"   // edit_set_text -- declared there, not here

// Callees of this TU that another header owns, listed so that nobody declares one of them
// here a second time: dlg_load, dlg_deactivate, dlg_run_modal, snd_set_btn_click and
// app_add_exit in dlg_dlg.h; ctrl_find_ndx, ctrl_set_text and ctrl_set_update in
// dlg_ctrl.h; timer_create and timer_remove_node in minimap.h; cur_hide and cur_show in
// TrigAct.h; SMemAlloc, SMemFree and SStrCopy in Storm.h; SStrPack in replay.h;
// NetworkMode in net_glue.h; sub_49E3F0 in gamesnd.h; img_update and vid_restore_palette
// elsewhere. Naming a header here does not pull it in -- a TU that calls one of these
// includes that header itself -- and memset and memmove come from <string.h>.

// --- nullary ---------------------------------------------------------------
//
// deactivate_popup and deactivate_modal_popup both tail-jmp dlg_deactivate, so their
// derived arity is only a lower bound; glue_panic_fallback_fn calls each with nothing in
// ECX or EDX, which settles them at zero. cur_animate likewise tail-jmps cur_update, and
// cursor_update calls it with ECX unset.
void deactivate_popup();         // 0x45FD20 (37 B) -- tail jmp dlg_deactivate
void deactivate_modal_popup();   // 0x457380 (26 B) -- tail jmp dlg_deactivate
void cur_animate();              // 0x43E410 (51 B) -- tail jmp cur_update
void light_cycle_init();         // 0x4667D0 (55 B)
void net_error_actualize();      // 0x474980 (128 B)

// 0x5085C8 read and returned, nothing else. Every caller consumes the full
// EAX with `test eax, eax`.
int IsSpawnedCopy();

// Both are `mov eax, dword ptr [<global>] / ret` -- an open archive handle.
// load_cd_binary and HasEitherCampaign both use the full EAX.
intptr_t get_cd_archive();            // 0x4658B0 (6 B) -- reads 0x66CCCC
intptr_t get_patch_archive();         // 0x4662B0 (6 B)

// --- one register argument -------------------------------------------------

// 0x43E5A0. ECX is a packed path buffer: glues_init_cursors builds it with
// SStrCopy + SStrPack into `[ebp-104h]` and passes `lea ecx, [ebp-104h]`.
// Both callers consume the full EAX (`mov [esi+104h], eax`), so it returns.
intptr_t cur_load(const char *pszPath);

// 0x444520. ECX is a file handle; all four callers discard EAX.
void CloseFile(intptr_t hFile);

// 0x49E740. `mov ecx, 0A5h / call sub_49E740` -- a small integer id, not a pointer; 22
// call sites and every one discards EAX. The return is int all the same: both exits
// materialise EAX (`call sub_49E4E0 / ret` and `xor eax,eax / ret`), and compiled void
// the body comes out 63 bytes against the oracle's 58.
int sub_49E740(int nId);

// 0x4C84C0. ECX is the dialog whose FLC child timers are being torn down;
// glues_dlg_deactivate passes its own incoming pDlg straight through.
void flc_remove_timers(ctrl_t *pDlg);

// 0x4C4EB0 / 0x4C4F30. ECX only, EAX discarded by every caller.
void vid_fade_in(int nArg0);
void vid_fade_out(int nArg0);

// --- two register arguments ------------------------------------------------

// 0x444530. ECX + EDX, no stack. All four callers use the full EAX.
int GetFileSize_gds(intptr_t nArg0, intptr_t nArg1);

// edit_set_text (0x4C8110) is not declared here: src117/h/save_dlg.h declares it, and
// this header includes that one.

// effects_init (0x4CA420) is not declared here either. It is declared in
// src117/h/dlg_effects.h, the defining TU's header, with the parameter types
// src117/lang/opt_dlgs.cpp derived -- `BYTE *pPalette` is the game palette at 0x666568
// and `void *pDesc` a descriptor built on the stack. glues.cpp includes that header for
// its own `effects_init(0, 0)` call.

// str_get_string and str_load_table are declared in dlg_str.h, included above.

// --- register + stack ------------------------------------------------------

// 0x4445F0, `ret Ch` -> ECX + EDX + 3 stack words. Every ret materialises 0 or
// 1 and both callers use the full EAX.
BOOL OpenFileEx(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3, int nArg4);

// 0x444640, `ret 8h` -> ECX + EDX + 2 stack words. All seven callers discard.
void ReadFile_gds(intptr_t nArg0, intptr_t nArg1, int nArg2, intptr_t nArg3);

// 0x4444E0, `ret 14h` -> ECX + EDX + 5 stack words. All fifteen callers discard.
void GameLoadImage(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3, intptr_t nArg4,
                   intptr_t nArg5, intptr_t nArg6);

// 0x4C2CA0, `ret 8h` -> ECX + EDX + 2 stack words.
intptr_t dlg_load_theme(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3);

// 0x444930 (gamedata.cpp). glue_load_cd_dlg installs it as dlg_load's PFN_DLGMEASURE;
// the signature is replay.cpp's, which needs the same one.
// Hyperion: pData and pA are const, which the string literals its callers pass need
// under clang and gcc; the body only reads pData's name and never reads pA.
void alloc_load_bmp(const void *pData, void *pOut, DWORD_PTR dwZero, const void *pA, DWORD dwB);

// 0x444CA0, `ret 4h` -> ECX + EDX + 1 stack word, and gluestrings_exit names every one:
// `mov ecx, [0x66A46C]` the block, `mov edx, 4EDA70h` = "glues.cpp", `push 2B1h` = line
// 689. The file and line tag the free, the same shape as SMemFree. This declaration is
// about 0x444CA0, which the ledger names; gametype.cpp names 0x40A3C0 for mem_free, and
// that is not settled here.
void mem_free(void *pMem, const char *pszFile, int nLine);

// The TU's own free functions, in oracle address order.

// 0x457400 (156 B). Opens a file out of the CD or patch archive, sizes it, SMemAllocs a
// buffer and reads it. It returns that buffer rather than a control, and both exits
// materialise EAX (`xor eax, eax` and `mov eax, esi`), so void cannot emit these bytes;
// glue_load_cd_dlg hands 0x457400 to dlg_load in EDX as a PFN_DLGLOADER and casts at the
// install site, as replay.cpp does for alloc_load_bmp. `ret 0Ch`, so ECX + EDX + three
// stack words: ECX is OpenFileEx's name, EDX a caller-supplied buffer whose SMemAlloc is
// skipped when it is non-null, [ebp+08h] an optional size-out that is null-checked before
// it is written, and [ebp+0Ch] and [ebp+10h] SMemAlloc's owner tag and line.
void *load_cd_binary(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);

// 0x4574A0 (68 B). A Win32 enumeration callback, so __stdcall is forced by the API that
// calls it: `ret 8h` with no register input.
BOOL __stdcall EnumFunc(intptr_t nArg0, int nArg1);

// 0x4574F0 (5 B). `jmp cur_animate` and nothing else.
void glue_cur_animate_fn();

// 0x457500 (5 B). `and dword ptr [ecx+18h], -11h` -- ECX is the control and +0x18 its
// flag word. One bit is cleared, not two: the encoding is `83 61 18 EF`, an imm8 of 0xEF
// sign-extended, whose only clear bit is 0x10, and `~0x10 == -0x11`. dialog.h names 0x10
// locatable, which is the tab-stop bit.
void remove_ctrl_tabstop(ctrl_t *pCtrl);

// 0x457990 (31 B). Frees the glue string table through mem_free.
void gluestrings_exit();

// 0x4579B0 (105 B). ECX only. The pointer types here are the ones replay.cpp and
// net_cmds.cpp derived at their own call sites -- `ctrl_t *` rather than `int`, and
// `char *` for glues_get_str, whose result replay_dlg_activate hands straight to
// ctrl_set_text.
void glue_screen_activate(ctrl_t *pDlg);

// 0x457A20 (26 B). The callee ignores ECX and all four callers set it, so the argument is
// declared even so: without it the caller's register allocation comes out wrong.
// do_load_dlg (gluLoad.cpp) passes `mov ecx, 4EDB38h`, a `.rdata` dialog template, so the
// parameter is that template's address. All four callers use the full EAX.
intptr_t glue_load_cd_dlg(intptr_t nArg0);

// 0x457B50 (232 B). The family's shared teardown.
void glues_clear_all();

// A control a glue slide-in keeps hidden until it is home; defined in glues.cpp.
struct _HIDDENCTRL;

// The hidden-control list, spelled as the ledger spells its Clear row so gen
// can seat it.
#pragma pack(push, 1)
template <> struct TSList<_HIDDENCTRL,TSGetLink<_HIDDENCTRL> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_HIDDENCTRL> link;
  // 0x4573A0 (84 B). Unlinks and frees every hidden control.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<_HIDDENCTRL,TSGetLink<_HIDDENCTRL> > HIDDENCTRLLIST;
OC_X86_LAYOUT(sizeof(HIDDENCTRLLIST) == 12, "HIDDENCTRLLIST size");
static_assert(offsetof(HIDDENCTRLLIST, _unmapped_00) == 0x00, "HIDDENCTRLLIST::_unmapped_00");
static_assert(offsetof(HIDDENCTRLLIST, link) == 0x04, "Clear reads [this+8] as link.next");

void glue_fade_out();            // 0x457C40 (15 B)
void CloseAllWindows();          // 0x457CF0 (57 B)
void set_netmode_return_menu();  // 0x457E50 (120 B)
void glues_reset_prev_pal();               // 0x457E40 (11 B)
void glue_panic_fallback_fn();   // 0x458440 (65 B)

// 0x457C50 / 0x457C70. ECX is the destination buffer and EDX its size --
// both end in `push edx / push <src> / push ecx / call SStrCopy`, which is
// SStrCopy(dest = ECX, src, cchDest = EDX).
void glues_get_path(char *pszDest, int cchDest);
void glues_get_pal_path(char *pszDest, int cchDest);

// 0x457CA0 (76 B). One register argument; tail-jumps str_get_string and nine
// callers use the full EAX. A string getter, so `char *`, not `int`.
char *glues_get_str(int nStrId);

// 0x457D30 (214 B). ECX + EDX. Absolutises a path against the root string at 0x66A368.
// arg1 is a pointer, not an `int`: `mov edi, edx` then `cmp byte ptr [edi], 5Ch`
// dereferences EDX as a byte, a backslash. Which argument is the source and which the
// destination is not derived, so the names stay neutral.
void glues_make_full_path(void *pArg0, char *pArg1);

// 0x457E10 (37 B). ECX + EDX + 1 stack word.
void glues_set_skip(int nArg0, int nArg1, int nArg2);

// 0x457ED0 (38 B). ECX + EDX + 1 stack word; forwards to ctrl_find_ndx then
// ctrl_set_text / edit_set_text.
void glue_set_text(ctrl_t *pDlg, short nIndex, char *pszText);

// 0x458380 (50 B). ECX is the dialog being torn down; no caller uses EAX.
void glues_dlg_deactivate(ctrl_t *pDlg);

// 0x4583C0 (125 B). ECX + EDX + 1 stack word. arg0 is the dialog, not an `int`: it goes
// to ctrl_find_ndx and to timer_create in ECX, and both take `ctrl_t *`. A pointer and an
// int occupy the same slot, so the bytes cannot separate them; the callees can.
void sub_4583C0(ctrl_t *pDlg, int nIndex, int bIn);

// 0x458490 (74 B). `ret 4h` with no register input, which is one stack argument and
// __stdcall. A callback: glues_init_btn installs it with
// `mov ecx, 458490h / jmp snd_set_btn_click`. The argument is spelled `dlg_dword_t` to
// fit the typedef it is installed through, dlg_dlg.h's
// `void(__stdcall *PFN_BTNCLICKSND)(dlg_dword_t)`; with `int` the install needs a cast.
void __stdcall sub_458490(dlg_dword_t dwSound);

// 0x4584E0 / 0x458550 / 0x4585E0 / 0x458690 / 0x458740. All nullary.
void glues_init_cursors();
void glues_init_grp();
void glues_init();
void glues_init_font();
void glues_init_btn();

// 0x4587A0 (450 B) / 0x458980 (355 B). The two transition step callbacks, installed by
// address: sub_4583C0 hands one of them to timer_create. One argument each, not nullary --
// each body opens `mov ebx, ecx` or `mov esi, ecx`, so ECX is the owning dialog.
void glues_transition_in_cb(ctrl_t *pDlg);
void glues_transition_out_cb(ctrl_t *pDlg);

// 0x458790 (10 B). `call timer_remove_node / jmp net_error_actualize`, and
// timer_remove_node takes ECX + EDX -- so both registers are live-in and are
// forwarded, never read here. Two arguments, not zero.
void glue_deferred_error_check(intptr_t nArg0, int nArg1);


// Two more bodies of this TU, declared here because the four glu* dialog TUs call them.

// 0x457F00 (511 B) -- `ret 8h`, ECX + EDX + two stack words. ECX is the owning dialog
// (gluConn_dlg_init, gluJoin_dlg_fcn and gluload_dlg_fcn all pass their own pDlg); EDX is
// a `.rdata` address at every site (0x503DB0, 0x503A00, 0x508120, ...) -- the per-control
// event-function table, with the first pushed word its size in bytes, the same
// (table, cbTable) pair ctrl_set_evt_fcns takes in dlg_ctrl.h. The second pushed word is
// a flag, 0 at every site seen.
void glue_init_screen_dlg(ctrl_t *pDlg, PFN_CTRL_EVT *pTable, DWORD cbTable,
                          intptr_t nFlags);

// 0x458100 (613 B) -- `ret Ch`, ECX + EDX + three stack words. EDX is a `.rdata` address
// at every site (0x508100, 0x508160). [ebp+08h] is the record count, read once as
// `mov si, word ptr [ebp+8h]`, so a WORD; the later dword writes there are a local
// reusing the dead home. [ebp+0Ch] is never read, so only its 4-byte footprint is known.
void glues_transition_in(intptr_t nArg0, void *pTable, WORD nCount, int nArg3, int nArg4);

// Callees of this TU's remaining bodies that no other header of ours declares.

// 0x4B55B0 close_title_dlg (26 B) / 0x466970 light_init (17 B) /
// 0x474B70 reset_net_error (8 B). All nullary, all plain `ret`, so every convention is
// byte-identical for them. RunGlues calls all three.
void close_title_dlg();
void light_init();
void reset_net_error();

// The three per-race ready screens are declared in gluRdyP.h, gluRdyT.h and gluRdyZ.h.

// 0x461940 (546 B), gluScore.cpp. The callee ignores ECX and its one caller sets it, so
// the argument is declared even so: without it RunGlues' register allocation comes out
// wrong. Its width and type are not visible from the callee, so `int` is a placeholder.
void do_score_dlg(int nArg0);

// 0x457510 (1044 B) -- this TU's own row and the menu system's outer loop: nullary. It
// reaches thirteen callees, which is what the declarations above are for; four of them --
// HasEitherCampaign, do_join_dlg, do_load_dlg and do_main_dlg -- come from gluMain.h,
// gluJoin.h and gluLoad.h, which glues.cpp includes rather than repeating them.
void RunGlues();

// 0x457A40 glues_transition_out (259 B) is this TU's row but is declared in
// src117/h/gluRdy.h as `BYTE glues_transition_out(ctrl_t *pDlg)`, derived there from
// glurdy_dlg_init widening the result with `movzx eax, al`; glues.cpp includes that
// header.

#endif  // OC117_GLUES_H
