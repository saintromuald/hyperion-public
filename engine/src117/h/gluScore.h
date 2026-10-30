// gluScore.h -- declarations for src117/lang/gluScore.cpp: the end-of-game
// score screen and its battle.net glue
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUSCORE_H
#define OC117_GLUSCORE_H

#include "platform.h"
#include "dialog.h"   // ctrl_t

// scorerow_t -- one player's row in the end-of-game score screen.
//
// The stride is exact: gluScore_tab_ctrl_updatetab memsets 0x12E0 bytes at
// 0x66B430 and sub_4630C0 walks the same block with `add ebx, 25Ch` and copies
// a row with `mov ecx, 97h / rep movsd` (0x97 * 4 == 0x25C), so the block is 8
// rows of 0x25C. Each measured quantity is a group of four parallel arrays:
// the raw value, the animated counter, the animation target and the scale.
struct scorerow_t {
  // +0x00 the row's name control. sub_4626E0 stores the control it has just
  // shown here (`mov dword ptr [edi], esi`) and sub_462C50 is its draw fcn.
  /*0x000*/ ctrl_t *pCtrlName;
  // +0x04 the three bar controls, stored through a cursor that starts at
  // row+4 and steps `add eax, 4h` over three iterations (sub_4626E0).
  /*0x004*/ ctrl_t *pCtrlBar[3];
  // +0x10 the total control (`mov dword ptr [edi+10h], esi`, sub_4626E0).
  /*0x010*/ ctrl_t *pCtrlTotal;
  // +0x14 "this row is in use". sub_4629F0 stores 1; sub_4623C0 and sub_4630C0
  // both reject the row when it is 0.
  /*0x014*/ LONG bUsed;
  // +0x18 the slot this row reports on. sub_4629F0 stores the loop index;
  // every reader turns it into `[slot*9*8 + 0x66C710]`, a 72-byte entry.
  /*0x018*/ LONG nSlot;
  // +0x1C three 0x80-byte captions, one per bar: sub_4626E0 hands ctrl_set_text
  // a cursor that starts at row+0x1C and steps `sub esi, -80h` three times.
  /*0x01C*/ char szBar[3][0x80];
  // +0x19C the three raw values the tab's fill callback writes.
  /*0x19C*/ LONG lValue[3];
  // +0x1A8 the animated counters, in scaled units. sub_4628C0 draws
  // `[row+k*4+1A8h] / [row+k*4+1C0h]`.
  /*0x1A8*/ LONG lAnim[3];
  // +0x1B4 the animation targets, value * scale (sub_4629F0: `imul ecx, eax`
  // then `mov dword ptr [esi+18h], ecx` at esi = row+0x19C+k*4).
  /*0x1B4*/ LONG lTarget[3];
  // +0x1C0 the per-bar scale, 1, 8 or 16 (sub_4629F0 stores it at `[esi+24h]`).
  /*0x1C0*/ LONG lScale[3];
  // +0x1CC the total's caption; sub_4626E0 passes row+0x1CC to ctrl_set_text.
  /*0x1CC*/ char szTotal[0x80];
  // +0x24C the total and its three parallel slots, in the per-bar order:
  // sub_4629F0 reads `[ebx]` and writes `[ebx+8h]` and `[ebx+0Ch]` at row+0x24C.
  /*0x24C*/ LONG lTotal;
  /*0x250*/ LONG lTotalAnim;
  /*0x254*/ LONG lTotalTarget;
  /*0x258*/ LONG lTotalScale;
};

OC_X86_LAYOUT(sizeof(scorerow_t) == 0x25C, "scorerow_t       sub_4630C0: rep movsd with ecx = 97h");
static_assert(offsetof(scorerow_t, pCtrlName) == 0x000, "scorerow_t::pCtrlName   sub_4626E0: mov dword ptr [edi], esi");
OC_X86_LAYOUT(offsetof(scorerow_t, pCtrlBar) == 0x004, "scorerow_t::pCtrlBar    sub_4626E0: lea eax, dword ptr [edi+4h]");
OC_X86_LAYOUT(offsetof(scorerow_t, pCtrlTotal) == 0x010, "scorerow_t::pCtrlTotal  sub_4626E0: mov dword ptr [edi+10h], esi");
OC_X86_LAYOUT(offsetof(scorerow_t, bUsed) == 0x014, "scorerow_t::bUsed       sub_4629F0: mov dword ptr [ebx-238h], 1h");
OC_X86_LAYOUT(offsetof(scorerow_t, nSlot) == 0x018, "scorerow_t::nSlot       sub_4629F0: mov dword ptr [ebx-234h], esi");
OC_X86_LAYOUT(offsetof(scorerow_t, szBar) == 0x01C, "scorerow_t::szBar       sub_4626E0: add edi, 1Ch then sub esi, -80h");
OC_X86_LAYOUT(offsetof(scorerow_t, lValue) == 0x19C, "scorerow_t::lValue      sub_462C10: mov dword ptr [ecx+19Ch], eax");
OC_X86_LAYOUT(offsetof(scorerow_t, lAnim) == 0x1A8, "scorerow_t::lAnim       sub_4628C0: mov eax, dword ptr [ecx+esi*4+1A8h]");
OC_X86_LAYOUT(offsetof(scorerow_t, lTarget) == 0x1B4, "scorerow_t::lTarget     sub_4629F0: mov dword ptr [esi+18h], ecx");
OC_X86_LAYOUT(offsetof(scorerow_t, lScale) == 0x1C0, "scorerow_t::lScale      sub_4629F0: mov dword ptr [esi+24h], eax");
OC_X86_LAYOUT(offsetof(scorerow_t, szTotal) == 0x1CC, "scorerow_t::szTotal     sub_4626E0: lea edx, dword ptr [edi+1CCh]");
OC_X86_LAYOUT(offsetof(scorerow_t, lTotal) == 0x24C, "scorerow_t::lTotal      sub_462C10: mov dword ptr [ecx+24Ch], eax");
OC_X86_LAYOUT(offsetof(scorerow_t, lTotalAnim) == 0x250, "scorerow_t::lTotalAnim  by position between lTotal and lTotalTarget");
OC_X86_LAYOUT(offsetof(scorerow_t, lTotalTarget) == 0x254, "scorerow_t::lTotalTarget sub_4629F0: mov dword ptr [ebx+8h], ecx");
OC_X86_LAYOUT(offsetof(scorerow_t, lTotalScale) == 0x258, "scorerow_t::lTotalScale sub_4629F0: mov dword ptr [ebx+0Ch], eax");

// The free functions.

// The eight slot records at 0x66C710; defined in gluScore.cpp.
struct scoreslot_t;

// A tab's row fill callback: sub_462B20, sub_462B60, sub_462BB0, sub_462C10.
typedef void (*PFN_SCOREFILL)(scorerow_t *pRow, int nPlayer);

void gluscore_dlg_deactivate(ctrl_t *pDlg);

// Record which slot a shuffled player came from. Only DL is stored, but the one
// caller hands over the zero-extended index it also subscripts with, so the
// second argument is an int.
void sub_461B70(int nSlot, int nOrigSlot);

// ECX only, stored whole; `ret` with no stack bytes and the caller discards EAX.
void gluscore_set_loaded(int bLoaded);

// The four tab fill callbacks. gluScore_tab_ctrl_updatetab pushes one of them
// as sub_4629F0's last argument, and sub_4629F0 calls it with the row in ECX
// and the slot entry's player number in EDX.
void sub_462B20(scorerow_t *pRow, int nPlayer);
void sub_462B60(scorerow_t *pRow, int nPlayer);
void sub_462BB0(scorerow_t *pRow, int nPlayer);
void sub_462C10(scorerow_t *pRow, int nPlayer);

// The total control's PFN_CTRL_DRAW; sub_4626E0 installs it at ctrl_t +0x2E.
void sub_462E00(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// Print a duration into a caller-supplied buffer. ECX is the buffer, EDX the
// seconds, and the one stack dword is the buffer size (`ret 4h`). Both
// divisions are unsigned and the range test is `jb`.
void sub_4618A0(char *pszBuf, unsigned int nSeconds, unsigned int cchBuf);

// Build the league result report. ECX and EDX are the header buffer and its
// size, the two stack dwords the detail buffer and its size (`ret 8h`); the one
// caller, net_end_game, tests EAX.
int __fastcall sub_461B80(char *pszHeader, int cchHeader, char *pszDetail, int cchDetail);

// The save-replay button's PFN_CTRL_EVT and the dialog's own event handler.
int gluScore_flc_saveReplay_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
int gluscore_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The tab control's PFN_CTRL_DRAW.
void gluScore_tab_ctrl_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x462160, the score dialog's activate; called by gluscore_dlg_fcn's arm 0.
void gluscore_dlg_activate(ctrl_t *pDlg);

// Rank one row for the sort. EDX is the row's index; the result is compared
// with `jle` by the only caller, so it is signed.
int sub_4623C0(scorerow_t *pRow, int nIndex);

// 0x4625F0, called at the tail of gluscore_dlg_activate. ECX only, `ret` with
// no stack bytes, and the caller discards EAX.
void sub_4625F0(ctrl_t *pDlg);

// The tab control's PFN_CTRL_EVT and the tab switch it drives. The switch
// takes the tab in ECX and returns nothing.
int gluScore_tab_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
void gluScore_tab_ctrl_updatetab(ctrl_t *pCtrl);

// 0x462F70, the score screen's timer callback. ECX is the dialog and the
// timer is removed from it once every counter has reached its target.
void sub_462F70(ctrl_t *pDlg);
void sub_4629F0(scorerow_t *pRows, scoreslot_t *pSlots, int nCount,
                PFN_SCOREFILL pfnFill);
void sub_4630C0(scorerow_t *pRows, int nCount);
void sub_4626E0(ctrl_t *pDlg);
void sub_4628C0(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void sub_462C50(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);
void sub_461E00();

// The two owner-drawn player controls, both PFN_CTRL_EVT.
int gluScore_player_scorebox_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
int gluScore_player_inset_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C7540 btn_ctrl_radiobutton (80 B), declared here although it belongs in a
// shared dialog header. ECX is a ctrl_t * and EDX a dlgevt_t *, read as both
// through this body; every path sets an EAX the body never reads and all four
// callers discard.
BOOL btn_ctrl_radiobutton(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_GLUSCORE_H
