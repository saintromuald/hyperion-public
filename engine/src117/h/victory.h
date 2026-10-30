// victory.h -- the interface of src117/lang/victory.cpp (oracle
// 0x4BD460..0x4BE500): the trigger execution state and the victory and defeat
// bookkeeping around it. A map's triggers live in eight per-player Storm lists;
// each game tick walks them, evaluates each trigger's 16 conditions and runs
// its 64 actions.
#pragma once

// platform.h comes first and not in alphabetical order: TrigAct.h pulls in
// CUnit.h, whose fields are spelled BYTE/WORD/DWORD, so sorting these three
// names would leave the header compiling only for an includer that defined the
// scaffold already.
#include "platform.h"
#include "gds_local.h"  // psnprintf

#include "TSList.h"
#include "TrigAct.h"

// TrigCondition -- 20 B, 16 to a trigger. trigger_cond_parse (0x4BDED0) walks
// them with a 20-byte stride, skips one whose bFlags bit 1 is set, and
// dispatches on bCondition through sgConditionFcns at 0x50E058. The fields the
// handlers in src117/lang/TrigCond.cpp read:
//   bRegion      a location id, 1-based: 0 means no location.
//   dwGroup      a nation, or one of the pseudo-groups the enumerators expand
//                into concrete nations (TrigEnum.h), in the same slot.
//   dwQualifier  the count or time compared against, unsigned at every site.
//   bComparison  0 at least, 1 at most, 10 exactly, the three the tail accepts.
//   bResType     one slot, three roles: a resource type, a score type, or a
//                switch index 0..255 into the 8-dword bitfield at 0x534F20.
// +0x12..+0x13 are unnamed: no handler touches either byte.
struct TrigCondition {
  /*0x00*/ BYTE bRegion;
  /*0x01*/ BYTE _unmapped_01[3];
  /*0x04*/ DWORD dwGroup;
  /*0x08*/ DWORD dwQualifier;
  /*0x0C*/ WORD wUnitType;
  /*0x0E*/ BYTE bComparison;
  /*0x0F*/ BYTE bCondition;
  /*0x10*/ BYTE bResType;
  /*0x11*/ BYTE bFlags;
  /*0x12*/ BYTE _unmapped_12[2];
};
static_assert(sizeof(TrigCondition) == 0x14, "add esi,14h in trigger_cond_parse");
static_assert(offsetof(TrigCondition, bRegion) == 0x00, "mov cl, byte ptr [esi]");
static_assert(offsetof(TrigCondition, dwGroup) == 0x04, "mov ecx, dword ptr [esi+4h]");
static_assert(offsetof(TrigCondition, dwQualifier) == 0x08, "mov edi, dword ptr [esi+8h]");
static_assert(offsetof(TrigCondition, wUnitType) == 0x0C, "mov dx, word ptr [esi+0Ch]");
static_assert(offsetof(TrigCondition, bComparison) == 0x0E, "movzx ecx, byte ptr [esi+0Eh]");
static_assert(offsetof(TrigCondition, bCondition) == 0x0F, "mov al, byte ptr [esi]");
static_assert(offsetof(TrigCondition, bResType) == 0x10, "movzx edx, byte ptr [esi+10h]");
static_assert(offsetof(TrigCondition, bFlags) == 0x11, "test byte ptr [esi+2h],2h");

// The condition handler. int, because the dispatch consumes the result with
// `test eax,eax`; __fastcall with one register argument, because the condition
// is passed in ECX alone and nothing cleans the stack.
typedef int(__fastcall *TRIGCONDPROC)(TrigCondition *pCond);

// TRIGGERNODE -- 0x968 B, a map trigger: a Storm list link, its 16 conditions,
// its 64 actions, the flags and the index of the action being run. The name is
// the oracle's own, from the RTTI type descriptor at 0x510940. The 27 bytes
// between dwFlags and bCurrentAction are unnamed because nothing reads them.
//
// The flag bits, each from the accessor that sets or reads it:
//   0x01  resuming -- trigger_parse skips both the condition pass and the
//         action-index reset when it is set
//   0x04  keep the current trigger     0x08  skip it entirely
//   0x10  aborted                      0x20  paused
//   0x40  abort disabled
struct TRIGGERNODE {
  /*0x000*/ TSLink<TRIGGERNODE> link;
  /*0x008*/ TrigCondition conditions[16];
  /*0x148*/ TrigAction actions[64];
  /*0x948*/ DWORD dwFlags;
  /*0x94C*/ BYTE _unmapped_94C[0x967 - 0x94C];
  /*0x967*/ BYTE bCurrentAction;
};
OC_X86_LAYOUT(sizeof(TRIGGERNODE) == 0x968, "push 968h at six allocation sites");
static_assert(offsetof(TRIGGERNODE, link) == 0x000, "trigger_parse walks [esi+4h]");
OC_X86_LAYOUT(offsetof(TRIGGERNODE, conditions) == 0x008, "lea ecx,[esi-0Fh] off +17h");
OC_X86_LAYOUT(offsetof(TRIGGERNODE, actions) == 0x148, "TrigAct.h");
OC_X86_LAYOUT(offsetof(TRIGGERNODE, dwFlags) == 0x948, "or dword ptr [eax+948h],40h");
OC_X86_LAYOUT(offsetof(TRIGGERNODE, bCurrentAction) == 0x967, "mov byte ptr [esi+967h],0");

// The eight per-player trigger lists. sgTriggers is 0x50E0D8; the 0x50E0E0 that
// sub_4BDDF0's induction variable starts from is &[0].link.next, the compiler
// folding +8 into the pointer, not a global of its own. The list class is
// spelled as the ledger spells its Clear row.
#pragma pack(push, 1)
template <> struct TSList<TRIGGERNODE,TSGetLink<TRIGGERNODE> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<TRIGGERNODE> link;
  // 0x4609C0 (84 B), in gluRdy.cpp. Unlinks and frees every trigger node.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<TRIGGERNODE, TSGetLink<TRIGGERNODE> > TRIGGERLIST;
OC_X86_LAYOUT(sizeof(TRIGGERLIST) == 12, "TSList.h layout");

// The TU's own functions.
void __fastcall CurrentTriggerDisableAbort(void);   // 0x4BD460
void __fastcall CurrentTriggerEnableAbort(void);    // 0x4BD470
void __fastcall CurrentTriggerPaused(void);         // 0x4BD480
void __fastcall CurrentTriggerUnpaused(void);       // 0x4BD490
int __fastcall IsCurrentTriggerAborted(void);       // 0x4BD560
void __fastcall KeepCurrentTrigger(void);           // 0x4BD5D0
BYTE __fastcall IsDelayedAction(int nPlayer);       // 0x4BD580
BYTE __fastcall PlayerGetVictoryCode(int nPlayer);  // 0x4BD8F0
void __fastcall PlayerSetVictoryCode(int nPlayer, BYTE bCode);   // 0x4BD900
void __fastcall SetDelayedAction(int nPlayer, DWORD dwTimer);    // 0x4BDA60
int __fastcall TriggerPlayer(void);                 // 0x4BDA70
int __fastcall IsOutOfGame(BYTE nPlayer);           // 0x4BD590
// The attribute is repeated here and not left on the definition alone:
// victory.cpp sees this declaration first, and the generated TUs see nothing
// else. Why it is needed at all is in the .cpp.
__declspec(noinline) int __fastcall trigger_cond_parse(TRIGGERNODE *pTrig);  // 0x4BDED0
void __fastcall trigger_parse(TRIGGERLIST *pList);  // 0x4BDFD0
void __fastcall victory_force_check(void);         // 0x4BE1C0
void __fastcall victory_notify_action_done(void);   // 0x4BE3A0
void __fastcall sub_4BDDE0(void);                   // 0x4BDDE0
void __fastcall sub_4BDDF0(void);                   // 0x4BDDF0
void __fastcall sub_4BE3D0(void);                   // 0x4BE3D0

// ExtMapRegions returns the base of the trigger-location table. `struct
// MapRegion;` rather than an include of TrigEnum.h: the return type is only
// ever a pointer here, and src117/gen/globals117.h forward-declares it the same
// way for gMapRegions. The definition, with its static_asserts, is TrigEnum.h's.
struct MapRegion;
MapRegion *__fastcall ExtMapRegions(void);          // 0x4BD550

void __fastcall TryAbortCurrentTrigger(void);       // 0x4BDA80
void __fastcall UpdateCountdownTimerString(void);   // 0x4BDE30
void __fastcall victory_free(void);                 // 0x4BE1D0
void __fastcall victory_update(void);               // 0x4BE340

// victory.cpp's own rows. VictDlg.cpp's siblings are declared in VictDlg.h.
int __fastcall LoadTriggers(void *pFile);           // 0x4BD5E0
int __fastcall SaveTriggers(void *pFile);           // 0x4BD920
void __fastcall MarkPlayerAsDefeated(BYTE nPlayer); // 0x4BD7A0
void victory_chk(void);                             // 0x4BE020
void __fastcall victory_disconnect(int nPlayer);    // 0x4BE190
void __fastcall victory_quit(int nPlayer);          // 0x4BE3E0
void timers_loop(int nTicks);                       // 0x4BE410


// Unported callees a landed body in this TU reaches, each declared here so the
// generated thunk restates its signature. EliminatedMsg's five callees are
// declared in the .cpp instead, alongside the held body that uses them.
void __fastcall trigger_execute(TRIGGERNODE *pTrig);    // 0x4BDF10 -- held

// 0x4BCF80 / 0x4BCFF0 (VictDlg.cpp). Both nullary: victory_update tail-jumps to
// each with no register set up, and a bare `ret` leaves no stack argument.
// do_victory_dlg's `xor eax,eax / ret` makes it int; do_loss_dlg tail-jumps into
// modeless_dlg_startup and returns nothing. victory_update discards both.
void __fastcall do_loss_dlg(void);
int __fastcall do_victory_dlg(void);

// 0x49DB50 / 0x4A73E0 / 0x46AEF0. All nullary: each loads its own ECX before
// any use, and no call site sets up an argument.
void __fastcall snd_end_streamed(void);
void __fastcall statport_trigger_reset(void);
void __fastcall map_msg_trigger_abort(void);

// 0x4A7960, 58 B (statres.cpp). One argument in ECX, copied by a 0x20-byte
// SStrCopy into 0x6AB400, so UpdateCountdownTimerString's buffer is 32 bytes.
void __fastcall SetCountdownText(const char *pszText);

// 0x4BD4A0 (173 B). It needs a slot in a header rather than a TU-local
// prototype because it is a hook target, not merely a callee; its own five
// callees stay declared in victory.cpp.
void __fastcall EliminatedMsg(int nPlayer);

