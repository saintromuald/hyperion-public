// gluChatSlot.cpp -- the lobby's per-player slot controls: the slot rows, their race,
// team and type combo handlers, their draw callbacks and the teardown paths. Bodies are
// in oracle VA order; the Storm file string "gluChatSlot.cpp" is at 0x4ED6C8, pushed as
// the owner tag of a free by sub_452360 and sub_4518D0.
//
// TU membership cuts both ways and needs re-checking after a change here: a callee
// defined in this file can be spliced into a caller in this file by /Ob2, which a
// cross-TU call never is, and a body whose size is exactly caller plus callee needs its
// callee here. __declspec(noinline) on the declaration is the lever.

// <string.h> for memset -- the spine's lever: MSVC expands a small memset
// inline as a float-domain zero plus an integer-domain store, which is the only
// spelling that reaches the oracle's xorps/movq mix. No CRT reference, no thunk.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include <string.h>

// The <stddef.h>-before-dialog.h precondition is now satisfied by gluChatSlot.h
// itself (59713da4). It was not when this TU was first written; the explicit
// include and the dialog.h-first reorder that stood here have been removed at
// the coordinator's instruction -- a duplicate here would hide the next
// regression. Canonical order: the TU's own header first.
#include "../h/gluChatSlot.h"
#include "../h/net_glue.h"
#include "../h/net_slot.h"
#include "../h/forces.h"
#include "../h/TrigEnum.h"
#include "../h/gluBNProfile.h"
#include "../h/net_cmds.h"
#include "../h/save_dlg.h"
#include "../h/gds_grp2bmpx.h"
#include "../h/gamedata.h"
#include "../h/gds_local.h"
#include "../h/dialog.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"
#include "../h/dlg_lbox.h"
#include "../h/gametype.h"
#include "../h/gluChat.h"
#include "../h/glues.h"
#include "../h/minimap.h"
#include "../h/pause.h"
#include "../h/Storm.h"
#include "../h/Teams.h"
#include "../h/uiMulti.h"
#include "../gen/globals117.h"

// The chat-room slot record: 0xB0 bytes, twelve of them at 0x6681B0. The walks step
// `add esi, 0B0h` under a counter of 12, and sub_4518D0 clears the array with one
// 0x840-byte memset, which is 12 * 0xB0.
//
// Each named field below is the width of the access that reads it, and that width has to
// stay: a WORD field declared DWORD loses its movzx. The gaps are _unmappedNN because
// nothing here reads them.
struct chatslot_t {
  // +0x00 the slot's state/kind. DWORD at every site, compared against small
  // literals: `cmp eax, 4h` / `cmp eax, 5h` (sub_451830, sub_453AE0),
  // `cmp eax, 8h` / `cmp eax, 5h` (sub_4523C0), `cmp dword ptr [edx], 1h`
  // (sub_4523C0), and `cmp dword ptr [esi-70h], ebx` against a 32-bit argument
  // (sub_4529A0). sub_453C80 stores a whole dword into it (`mov dword ptr
  // [ecx], edx`).
  /*0x00*/ DWORD dwState;
  // +0x04 the previous state. sub_453C80 copies +0x00 into it before
  // overwriting +0x00: `mov eax, dword ptr [ecx] / mov dword ptr [ecx+4h], eax`.
  /*0x04*/ DWORD dwPrevState;
  // +0x08 the slot's display name. sub_451830 takes its address for SStrCmp, so the
  // field is the buffer rather than a pointer to one. Its extent is not derived: 0x44 is
  // the distance to the next named field.
  /*0x08*/ char szName[0x44];
  // +0x4C the slot's race. BYTE: `mov cl, byte ptr [esi+4Ch]` twice in
  // sub_453A00, feeding CanChangeRace's BYTE first parameter and sub_4BAEA0's.
  // sub_4537C0 reads the same slot as a whole dword (`mov ecx, dword ptr
  // [esi+4Ch]`) and hands it to BanPlayer and its siblings.
  /*0x4C*/ union {
    BYTE bRace;
    int nPlayer;
  };
  // +0x50 the Storm player id: sub_452A00 `push dword ptr [edi]` (edi = +0x50)
  // into SNetGetPlayerCaps, sub_452AB0 indexes dword_6689F4 with it.
  /*0x50*/ DWORD dwPlayerId;
  // +0x54 a BYTE compared against a 32-bit slot id after widening:
  // `movzx eax, byte ptr [edx+54h] / cmp eax, edi` (sub_4523C0).
  /*0x54*/ BYTE b54;
  /*0x55*/ BYTE _unmapped55[0x58 - 0x55];
  // +0x58 "this row is on the local player's team". DWORD: sub_452BE0 tests
  // it and stores the `sete` of that comparison into it.
  /*0x58*/ DWORD dw58;
  // +0x5C BYTE, sub_452BE0's byte argument (`mov byte ptr [edi+5Ch], al`).
  /*0x5C*/ BYTE b5C;
  /*0x5D*/ BYTE _unmapped5D;
  // +0x5E / +0x60 the name control's left and top as the rows were laid out:
  // sub_4520C0 stores `word ptr [edi+5Eh]` and `[edi+60h]` from its rc.
  /*0x5E*/ short wX;
  /*0x60*/ short wY;
  /*0x62*/ BYTE _unmapped62[0x64 - 0x62];
  // +0x64 DWORD, stored 1 by sub_453C80 (`mov dword ptr [ecx+64h], 1h`).
  /*0x64*/ DWORD dw64;
  // +0x68 DWORD, stored 1 by sub_453B10 (`mov dword ptr [ecx+68h], 1h`).
  /*0x68*/ DWORD dw68;
  // +0x6C a timestamp. DWORD, written by sub_453B10, sub_453C80 and sub_453A00
  // with the identical three-instruction expression
  // `mov edx, [0x672658] / mov eax, [0x672660] / lea eax, [edx+eax*4]`.
  /*0x6C*/ DWORD dw6C;
  // +0x70 the pending value of whatever the row's combo edits. BYTE:
  // `movzx eax, byte ptr [esi]` at esi = base+0x70 (sub_4529A0),
  // `movzx eax, byte ptr [edx+70h]` (sub_4523C0), and sub_453A00 both reads it
  // (`mov al, byte ptr [esi+70h]`) and stores the incoming byte into it.
  /*0x70*/ BYTE b70;
  // +0x71 the value +0x70 held before the pending one. BYTE: sub_453A00 does
  // `mov al, byte ptr [esi+70h] / mov byte ptr [esi+71h], al`.
  /*0x71*/ BYTE b71;
  /*0x72*/ BYTE _unmapped72[0x74 - 0x72];
  // +0x74 the "this row has a change pending" flag. DWORD: sub_453A00 stores 1
  // (`mov dword ptr [esi+74h], 1h`), sub_4523C0 stores 0, sub_4529A0 tests it
  // with `cmp dword ptr [esi+4h], 0h` at esi = base+0x70.
  /*0x74*/ DWORD dw74;
  // +0x78 the value +0x79 was last formatted from, as a BYTE:
  // `movzx eax, byte ptr [esi+28h]` at esi = base+0x50 (sub_453CC0).
  /*0x78*/ BYTE bValShown;
  // +0x79 the value cell's text. sub_453CC0 psnprintfs "%3u" into it with a
  // count of 0x20 and hands it to ctrl_set_text.
  /*0x79*/ char szValText[0x20];
  /*0x99*/ BYTE _unmapped99[0x9C - 0x99];
  // +0x9C latency from the caps block: stored by sub_452A00, divided by 200
  // for the bar count in sub_452620.
  /*0x9C*/ DWORD dwLatency;
  // +0xA0 DWORD, written by sub_451830 with the function's second argument
  // (`mov dword ptr [esi+0A0h], eax`).
  /*0xA0*/ DWORD dwA0;
  // +0xA4 "stats requested" flag, set once sub_452AB0 has sent /stats.
  /*0xA4*/ DWORD dwA4;
  // +0xA8 flag word; sub_452D60 sets or clears bit 0x4000 in it.
  /*0xA8*/ DWORD dwA8;
  // +0xAC the row's first control. A ctrl_t *: sub_451890 loads it and hands it
  // straight to ctrl_gray's ECX, then walks `mov ecx, dword ptr [edi]` -- i.e.
  // dereferences it at +0x00, which is ctrl_t::pNext. sub_451AD0 chains four
  // deep the same way; sub_4529A0 and sub_4523C0 reach ctrl_t::dwFlags at +0x18
  // through it.
  /*0xAC*/ ctrl_t *pCtrl;
};

OC_X86_LAYOUT(sizeof(chatslot_t) == 0xB0, "chatslot_t: stride from `add esi, 0B0h` x12 and memset 0x840");
static_assert(offsetof(chatslot_t, dwState) == 0x00, "chatslot_t::dwState   sub_453AE0: mov eax, dword ptr [eax]");
static_assert(offsetof(chatslot_t, dwPrevState) == 0x04, "chatslot_t::dwPrevState sub_453C80: mov dword ptr [ecx+4h], eax");
static_assert(offsetof(chatslot_t, szName) == 0x08, "chatslot_t::szName    sub_451830: lea eax, dword ptr [esi+8h]");
static_assert(offsetof(chatslot_t, bRace) == 0x4C, "chatslot_t::bRace     sub_453A00: mov cl, byte ptr [esi+4Ch]");
static_assert(offsetof(chatslot_t, nPlayer) == 0x4C, "chatslot_t::nPlayer   sub_4537C0: mov ecx, dword ptr [esi+4Ch]");
static_assert(offsetof(chatslot_t, dwPlayerId) == 0x50, "chatslot_t::dwPlayerId sub_452A00: push dword ptr [edi], edi = +0x50");
static_assert(offsetof(chatslot_t, dw58) == 0x58, "chatslot_t::dw58      sub_452BE0: cmp dword ptr [edi+58h], 0");
static_assert(offsetof(chatslot_t, b5C) == 0x5C, "chatslot_t::b5C       sub_452BE0: mov byte ptr [edi+5Ch], al");
static_assert(offsetof(chatslot_t, dwLatency) == 0x9C, "chatslot_t::dwLatency sub_452620: mov edx, dword ptr [eax+9Ch]");
static_assert(offsetof(chatslot_t, dwA4) == 0xA4, "chatslot_t::dwA4      sub_452AB0: mov dword ptr [esi], 1, esi = +0xA4");
static_assert(offsetof(chatslot_t, dwA8) == 0xA8, "chatslot_t::dwA8      sub_452D60: mov eax, dword ptr [edi+0A8h]");
static_assert(offsetof(chatslot_t, b54) == 0x54, "chatslot_t::b54       sub_4523C0: movzx eax, byte ptr [edx+54h]");
static_assert(offsetof(chatslot_t, wX) == 0x5E, "chatslot_t::wX        sub_4520C0: mov word ptr [edi+5Eh], ax");
static_assert(offsetof(chatslot_t, wY) == 0x60, "chatslot_t::wY        sub_4520C0: mov word ptr [edi+60h], ax");
static_assert(offsetof(chatslot_t, dw64) == 0x64, "chatslot_t::dw64      sub_453C80: mov dword ptr [ecx+64h], 1h");
static_assert(offsetof(chatslot_t, dw68) == 0x68, "chatslot_t::dw68      sub_453B10: mov dword ptr [ecx+68h], 1h");
static_assert(offsetof(chatslot_t, dw6C) == 0x6C, "chatslot_t::dw6C      sub_453B10: mov dword ptr [ecx+6Ch], eax");
static_assert(offsetof(chatslot_t, b70) == 0x70, "chatslot_t::b70       sub_453A00: mov byte ptr [esi+70h], bl");
static_assert(offsetof(chatslot_t, b71) == 0x71, "chatslot_t::b71       sub_453A00: mov byte ptr [esi+71h], al");
static_assert(offsetof(chatslot_t, dw74) == 0x74, "chatslot_t::dw74      sub_453A00: mov dword ptr [esi+74h], 1h");
static_assert(offsetof(chatslot_t, bValShown) == 0x78, "chatslot_t::bValShown sub_453CC0: movzx eax, byte ptr [esi+28h], esi = +0x50");
static_assert(offsetof(chatslot_t, szValText) == 0x79, "chatslot_t::szValText sub_453CC0: lea edi, dword ptr [esi+29h], esi = +0x50");
static_assert(offsetof(chatslot_t, dwA0) == 0xA0, "chatslot_t::dwA0      sub_451830: mov dword ptr [esi+0A0h], eax");
OC_X86_LAYOUT(offsetof(chatslot_t, pCtrl) == 0xAC, "chatslot_t::pCtrl     sub_451890: mov edi, dword ptr [esi+0ACh]");

// ctrl_t's +0x24 and +0x26, reached through dialog.h's _unmapped24[6] window, in
// src117/lang/gluRdy.cpp's spellings: +0x24 is a WORD (`or word ptr [ecx+24h], ax`) and
// +0x26 a DWORD this file loads and immediately dereferences as a chatslot_t, so it is a
// `chatslot_t *`.
static __forceinline dlg_word_t &ctrl_w24(ctrl_t *p) {
  return *reinterpret_cast<dlg_word_t *>(&p->_unmapped24[0]);
}
static __forceinline chatslot_t *&ctrl_slot(ctrl_t *p) {
  return *reinterpret_cast<chatslot_t **>(&p->_unmapped24[2]);
}

// ctrl_t +0x48, the listbox selection byte, read directly by sub_4539A0
// (`cmp dl, byte ptr [esi+48h]`); lbox_get_selection reads the same field. It falls
// inside dialog.h's +0x46 union, and lbox_t is forward-declared only, so it is reached
// through the object rather than named.
static __forceinline BYTE &ctrl_b48(ctrl_t *p) {
  return CTRL_LBOX_SEL(p);  // Hyperion: was [0x48]
}

// The globals this file reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. dword_668190 and
// sgChatSlots_g carry the names the registry already has; the rest are local to this TU,
// and where the bytes do not give a role the name is the address.

// 0x6681B0 -- the twelve chat-room slot records derived above.
#ifdef OC_DLL_MODE
  #ifndef sgChatSlots_g
  #define sgChatSlots_g (*reinterpret_cast<chatslot_t(*)[12]>(OC_VA(0x6681B0u)))
  #endif
#else
extern chatslot_t sgChatSlots_g[12] OC_UNALIGNED;
#endif

// 0x668190 -- an array of eight pointers, freed one by one by sub_452360 with esi
// stepping 0 to 0x1C by 4, so 0x20 bytes. Nothing here dereferences one, so the pointee
// is unknown and the type is `void *`.
#ifdef OC_DLL_MODE
  #ifndef dword_668190
  #define dword_668190 (*reinterpret_cast<void *(*)[8]>(OC_VA(0x668190u)))
  #endif
#else
extern void *dword_668190[8] OC_UNALIGNED;
#endif

// 0x6689F4 -- eight DWORDs, one per slot: sub_4523A0 subscripts it with a slot index
// (`cmp dword ptr [ecx*4+6689F4h], eax`), and sub_4518D0 clears exactly 32 bytes of it,
// which ends where the next global starts at 0x668A14.
#ifdef OC_DLL_MODE
  #ifndef dword_6689F4
  #define dword_6689F4 (*reinterpret_cast<DWORD(*)[8]>(OC_VA(0x6689F4u)))
  #endif
#else
extern DWORD dword_6689F4[8] OC_UNALIGNED;
#endif

// 0x6689F0 -- the pointer sub_4518D0 hands to mem_free. Read as a whole dword
// (`mov ecx, dword ptr [0x6689F0]`) and never dereferenced here.
#ifdef OC_DLL_MODE
#define sgpChatSlotBuf (*reinterpret_cast<void **>(OC_VA(0x6689F0u)))
#else
extern void *sgpChatSlotBuf;
#endif

// 0x668A14 -- a DWORD "the slot controls are suppressed" flag, tested with
// `cmp dword ptr [0x668A14], 0h` in sub_452600 and sub_453A50 to decide whether
// to show a control, and cleared by sub_451AB0.
#ifdef OC_DLL_MODE
#define sgdwSlotsHidden (*reinterpret_cast<DWORD *>(OC_VA(0x668A14u)))
#else
extern DWORD sgdwSlotsHidden;
#endif

// 0x668A18 -- a DWORD "the slot controls are frozen" flag: sub_453A50 skips
// ctrl_ungray when it is non-zero, sub_451AD0 sets it to 1, sub_451DA0 tests it.
#ifdef OC_DLL_MODE
#define sgdwSlotsFrozen (*reinterpret_cast<DWORD *>(OC_VA(0x668A18u)))
#else
extern DWORD sgdwSlotsFrozen;
#endif

// 0x668A1C -- a DWORD deadline, compared unsigned against the tick counter
// 0x672658 (`mov eax, [0x672658] / cmp eax, [0x668A1C] / jb`) in sub_453A50 and
// cleared by sub_4518D0. The `jb` is what fixes the unsignedness.
#ifdef OC_DLL_MODE
#define sgdwSlotsDeadline (*reinterpret_cast<DWORD *>(OC_VA(0x668A1Cu)))
#else
extern DWORD sgdwSlotsDeadline;
#endif

// 0x668A24 -- a DWORD re-entrancy guard: sub_451AB0 and sub_451DA0 raise it to
// 1 around a refresh and sub_451DA0 drops it to 0 at the end.
#ifdef OC_DLL_MODE
#define sgdwRefreshing (*reinterpret_cast<DWORD *>(OC_VA(0x668A24u)))
#else
extern DWORD sgdwRefreshing;
#endif

// 0x668A28 -- a DWORD "a refresh is pending" flag. sub_453B10 and sub_453C80
// both raise it only when IsTVBGame() is true, through a `cmovnz` against the
// literal 1; sub_451DA0 tests and clears it.
#ifdef OC_DLL_MODE
#define sgdwRefreshPending (*reinterpret_cast<DWORD *>(OC_VA(0x668A28u)))
#else
extern DWORD sgdwRefreshPending;
#endif

// 0x668A20 -- `dword_668A20` in the sheet. A DWORD compared whole against
// sub_452F70's fourth argument (`cmp ecx, dword ptr [0x668A20]`) before the
// deadline test; what it holds is not derived.
#ifdef OC_DLL_MODE
#define dword_668A20 (*reinterpret_cast<DWORD *>(OC_VA(0x668A20u)))
#else
extern DWORD dword_668A20;
#endif

// 0x672658 -- the game's tick counter, `dword_672658` as net_mgr.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef dword_672658
  #define dword_672658 (*reinterpret_cast<DWORD *>(OC_VA(0x672658u)))
  #endif
#else
extern DWORD dword_672658;
#endif

// 0x672660 -- a DWORD read whole and scaled by 4 into a deadline against the tick
// counter: `mov eax, dword ptr [0x672660] / lea eax, dword ptr [edx+eax*4]`, in
// sub_453B10, sub_453C80 and sub_453A00.
#ifdef OC_DLL_MODE
#define sgdwSlotTimeout (*reinterpret_cast<DWORD *>(OC_VA(0x672660u)))
#else
extern DWORD sgdwSlotTimeout;
#endif

// 0x6726D0 -- the eight 36-byte player records. GetNumTeamOccupiedSlots walks them from
// 0x6727CC down to 0x6726D0 in 0x24 steps, and both it and sub_4517D0 read the team byte
// at +2 off that same stride. The other fields are not derived here.
struct netslot_t {
  /*0x00*/ BYTE bType;
  // +0x01 the race: sub_452440 `mov al, byte ptr [eax+6726D1h]`.
  /*0x01*/ BYTE bRace;
  // +0x02 the team. BYTE at both sites named above.
  /*0x02*/ BYTE bTeam;
  // +0x03 the player NAME, handed to _stricmp by sub_452440 (`lea eax,
  // [eax*4+6726D3h]`). 25 bytes, the rest of the record.
  /*0x03*/ char szName[0x24 - 0x03];
};
static_assert(sizeof(netslot_t) == 0x24, "netslot_t: GetNumTeamOccupiedSlots walks 0x6727F0-0x24 down to 0x6726D0 in 0x24 steps");
static_assert(offsetof(netslot_t, bRace) == 0x01, "netslot_t::bRace sub_452440: mov al, byte ptr [eax+6726D1h]");
static_assert(offsetof(netslot_t, szName) == 0x03, "netslot_t::szName sub_452440: lea eax, [eax*4+6726D3h]");
static_assert(offsetof(netslot_t, bType) == 0x00, "netslot_t::bType GetNumTeamOccupiedSlots: mov al, byte ptr [edx]");
static_assert(offsetof(netslot_t, bTeam) == 0x02, "netslot_t::bTeam sub_4517D0: mov cl, byte ptr [eax*4+6726D2h]");

#ifdef OC_DLL_MODE
#define sgNetSlots (*reinterpret_cast<netslot_t(*)[8]>(OC_VA(0x6726D0u)))
#else
extern netslot_t sgNetSlots[8];
#endif

// Hyperion: sgNetSlots[g_LocalNationID] as the 32-bit original addresses it. A joiner's
// rows are drawn before 1.17 has given it a slot, g_LocalNationID still 0xFFFFFFFF, and the
// original's index wraps to the row before the table, which it reads harmlessly; a 64-bit
// address does not wrap, and faulted there (a LAN room's joiner).
static netslot_t &LocalNetSlot(void)
{
  return *reinterpret_cast<netslot_t *>(reinterpret_cast<char *>(&sgNetSlots[0]) +
                                         static_cast<int>(g_LocalNationID * sizeof(netslot_t)));
}

// 0x673D48 gNetPlayerData_g -- the chat-room slot records, spelled as
// forces.cpp and net_slot.cpp spell it (the registry's BYTE[0x1B0] is not
// emitted).
#ifndef OC_FORCES_gNetPlayerData_g
#define OC_FORCES_gNetPlayerData_g
#ifdef OC_DLL_MODE
#define gNetPlayerData_g (reinterpret_cast<PlayerData *>(OC_VA(0x673D48u)))
#else
extern PlayerTable<PlayerData, PlayerTagNetData> gNetPlayerData_g;
#endif
#endif

// 0x6728F5 -- unregistered. A BYTE widened to a dword the moment it is read:
// `movzx edi, byte ptr [0x6728F5]` (sub_451DA0), then used as sub_451B50's
// 32-bit second argument alongside the literal 8.
#ifdef OC_DLL_MODE
#define sgbSlotCount (*reinterpret_cast<BYTE *>(OC_VA(0x6728F5u)))
#else
extern BYTE sgbSlotCount;
#endif

// 0x67CC90 -- replay.cpp's `gPlayback`. sub_451DA0 tests it twice.
#ifdef OC_DLL_MODE
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern DWORD gPlayback;
#endif

// 0x6A5E34 -- saveload.cpp's `gpLoadGameFile`. sub_451DA0 tests it.
#ifdef OC_DLL_MODE
  #ifndef gpLoadGameFile
  #define gpLoadGameFile (*reinterpret_cast<void **>(OC_VA(0x6A5E34u)))
  #endif
#else
extern void *gpLoadGameFile;
#endif

// 0x6CE714 -- `gwUnk10_g` in re/globals117.json (dlg/dlg.cpp), typed WORD there.
// One of the theme metrics; sub_453B50 widens it with
// `movzx eax, word ptr [0x6CE714]` and pushes it to draw_local_ctrl.
#ifdef OC_DLL_MODE
  #ifndef gwUnk10_g
  #define gwUnk10_g (*reinterpret_cast<WORD *>(OC_VA(0x6CE714u)))
  #endif
#else
extern WORD gwUnk10_g;
#endif

// 0x6CF7A8 -- `sgpCurrentCombo_g` in re/globals117.json (dlg/lbox.cpp), typed
// `ctrl_t *` there. sub_451AD0 loads it straight into combo_reset_slider's ECX,
// whose parameter is an lbox_t *.
#ifdef OC_DLL_MODE
  #ifndef sgpCurrentCombo_g
  #define sgpCurrentCombo_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6CF7A8u)))
  #endif
#else
extern ctrl_t *sgpCurrentCombo_g;
#endif

// 0x6D300E -- `gbFontJust` in re/globals117.json (gds/font.cpp), typed BYTE.
// The set-then-call idiom in front of draw_ctrl_text: 0x21 in sub_453AE0 and
// 0x11 in sub_453B50.
#ifdef OC_DLL_MODE
  #ifndef gbFontJust
  #define gbFontJust (*reinterpret_cast<BYTE *>(OC_VA(0x6D300Eu)))
  #endif
#else
extern BYTE gbFontJust;
#endif

// 0x508D50 -- `g_LocalNationID` in re/globals117.json (net_glue.cpp), typed
// DWORD. sub_453A00 pushes it whole as CanChangeRace's stack
// argument (`push dword ptr [0x508D50]`), which is the same reading.
#ifdef OC_DLL_MODE
  #ifndef g_LocalNationID
  #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #endif
#else
extern DWORD g_LocalNationID;
#endif

// 0x4ED6C8 -- the .rdata literal "gluChatSlot.cpp" this TU hands its two deallocators as
// an owner tag. An object rather than a pointer: the address is the value, so
// `push 4ED6C8h` is 5 bytes where a pointer variable costs a 6-byte load.
#ifdef OC_DLL_MODE
#define aGluChatSlotCpp (*reinterpret_cast<char(*)[1]>(OC_VA(0x4ED6C8u)))
#else
extern char aGluChatSlotCpp[1];
#endif

// ---------------------------------------------------------------------------
// Callees and data the later bodies reach.

// 0x4B8E80, `xor eax, eax / ret`, spelled as VictDlg.cpp and opt_dlgs.cpp spell
// it; sub_453BC0 calls it for state 3 and ignores the result.
int __fastcall nullsub___shared_();

// 0x4A8320, spelled as mapmsg.cpp spells it.
void __fastcall get_printable_string(char *pszDst, const char *pszSrc, int cch);


// Storm ordinal 578, spelled as League.cpp spells it; caller-cleaned varargs.
extern "C" int __cdecl SStrPrintf(char *pszDest, size_t cchDest,
                                  const char *pszFmt, ...);

// The caps block SNetGetPlayerCaps fills: its size first (0x24, sub_452A00's
// frame), the latency at +0x18.
struct playercaps_t {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ DWORD _unmapped04[5];
  /*0x18*/ DWORD dwLatency;
  /*0x1C*/ DWORD _unmapped1C[2];
};
static_assert(sizeof(playercaps_t) == 0x24, "mov dword ptr [ebp-24h], 24h");
static_assert(offsetof(playercaps_t, dwSize) == 0x00, "mov dword ptr [ebp-24h], 24h");
static_assert(offsetof(playercaps_t, dwLatency) == 0x18, "mov eax, dword ptr [ebp-0Ch]");

// stru_503D0C -- the sheet's name; four {race, string id} pairs that
// sub_4538D0 walks with `add edi, 2h / cmp edi, 503D14h`.
struct raceopt_t {
  /*0x00*/ BYTE bRace;
  /*0x01*/ BYTE bStrId;
};
static_assert(sizeof(raceopt_t) == 2, "add edi, 2h");
static_assert(offsetof(raceopt_t, bRace) == 0x00, "movzx ecx, byte ptr [edi]");
static_assert(offsetof(raceopt_t, bStrId) == 0x01, "movzx ecx, byte ptr [edi+1h]");

#ifdef OC_DLL_MODE
#define stru_503D0C (*reinterpret_cast<raceopt_t(*)[4]>(OC_VA(0x503D0Cu)))
#else
extern raceopt_t stru_503D0C[4];
#endif

// stru_503B68 -- the thirteen slot-type options sub_4532B0 offers, 0x20 bytes
// each. The loop reads from the interior base 0x503B6C (`[esi-4]` is the type)
// and counts to 0Dh.
struct slottypeopt_t {
  /*0x00*/ DWORD dwType;
  /*0x04*/ DWORD dwStr;
  /*0x08*/ DWORD bIfArg2;
  /*0x0C*/ DWORD bIfNotArg2;
  /*0x10*/ DWORD bNeedsName;
  /*0x14*/ DWORD bIfArg3;
  /*0x18*/ DWORD bTeamGame;
  /*0x1C*/ DWORD bPlayback;
};
static_assert(sizeof(slottypeopt_t) == 0x20, "add esi, 20h");
static_assert(offsetof(slottypeopt_t, dwType) == 0x00, "mov edi, dword ptr [esi-4h]");
static_assert(offsetof(slottypeopt_t, dwStr) == 0x04, "mov ecx, dword ptr [esi]");
static_assert(offsetof(slottypeopt_t, bIfArg2) == 0x08, "cmp dword ptr [esi+4h], 0h");
static_assert(offsetof(slottypeopt_t, bIfNotArg2) == 0x0C, "cmp dword ptr [esi+8h], 0h");
static_assert(offsetof(slottypeopt_t, bNeedsName) == 0x10, "cmp dword ptr [esi+0Ch], 0h");
static_assert(offsetof(slottypeopt_t, bIfArg3) == 0x14, "cmp dword ptr [esi+10h], 0h");
static_assert(offsetof(slottypeopt_t, bTeamGame) == 0x18, "cmp dword ptr [esi+14h], 0h");
static_assert(offsetof(slottypeopt_t, bPlayback) == 0x1C, "cmp dword ptr [esi+18h], 0h");

#ifdef OC_DLL_MODE
#define stru_503B68 (*reinterpret_cast<slottypeopt_t(*)[13]>(OC_VA(0x503B68u)))
#else
extern slottypeopt_t stru_503B68[13] OC_UNALIGNED;
#endif

// The bodies, in oracle VA order.

// 0x4517D0, 71 B. May this slot's occupant be moved off its current team? Both arguments
// are spilled into the two halves of one register (`mov bl, dl` / `mov bh, cl`), which is
// what the body's `push ebx` is for, and GetNumTeamOccupiedSlots takes the team byte
// still live in CL from the compare above it, which a BYTE parameter buys. The shared
// tail is `return TRUE`, with the single FALSE arm the fall-through; one guarded
// early-out per condition would duplicate that tail.
BOOL sub_4517D0(BYTE bArg0, BYTE bArg1) {
  if (get_lobby_state() != 4 && IsTeamGame()) {
    BYTE bTeam0 = sgNetSlots[bArg0].bTeam;
    if (bTeam0 != sgNetSlots[bArg1].bTeam && GetNumTeamOccupiedSlots(bTeam0) == 1) {
      return FALSE;
    }
  }
  return TRUE;
}

// 0x451820, 11 B. Read the lobby mode and answer yes regardless.
//
// The call's result is discarded -- `call get_lobby_state / mov eax, 1h / ret` -- so
// the body is a side-effecting probe with a constant answer. Reproduced, not
// repaired.
BOOL sub_451820(BYTE bSlot, BYTE bType) {
  get_lobby_state();
  return TRUE;
}

// 0x451830, 85 B. Record a value against the slot whose name matches. The second
// argument is spilled to the stack and reloaded only on the hit path, which is where the
// `push ecx` frame comes from, while the first stays in EBX because every iteration uses
// it. SStrCmp is the Storm import at 0x4D019C; its third argument of 0x7FFFFFFF means no
// bound, and the call is callee-cleaned.
void sub_451830(intptr_t nArg0, int nArg1) {
  chatslot_t *p = sgChatSlots_g;
  for (int i = 0; i < 12; ++i) {
    if ((p->dwState == 4 || p->dwState == 5) &&
        SStrCmp(p->szName, reinterpret_cast<const char *>(nArg0), 0x7FFFFFFF) == 0) {
      p->dwA0 = nArg1;
      return;
    }
    ++p;
  }
}

// 0x451890, 57 B. Gray the first two controls of every active row. The `dec eax /
// cmp eax, 2h / jnbe` is the unsigned range test `(state - 1) <= 2`, what a
// `>= 1 && <= 3` pair on an unsigned field folds to, and the `lea ecx, dword ptr [ecx]`
// before the loop head is an alignment nop, never written in source. The second control
// is `pCtrl->pNext`, which is what makes +0xAC a `ctrl_t *`.
void sub_451890(intptr_t nArg0) {
  chatslot_t *p = sgChatSlots_g;
  int nLeft = 12;
  do {
    if (p->dwState - 1 <= 2) {
      ctrl_t *pCtrl = p->pCtrl;
      ctrl_gray(pCtrl);
      ctrl_gray(pCtrl->pNext);
    }
    ++p;
  } while (--nLeft);
}

// 0x4518D0, 98 B. Tear the chat-room slot state down. Both clears are the same
// `memset(&g, 0, sizeof g)` spelling and MSVC expands them differently by size: the 0x840
// one over the twelve records is a real call, the 0x20 one over the slot values is inlined
// as `xorps xmm0, xmm0` and two `movdqu` stores. `sizeof` has to resolve against an array
// in both forms, since over a pointer it collapses to 4. The trailing timer_remove_node is
// a tail jump, so it has to stay the last statement.
void sub_4518D0(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  memset(sgChatSlots_g, 0, sizeof(sgChatSlots_g));
  sgdwSlotsDeadline = 0;
  memset(dword_6689F4, 0, sizeof(dword_6689F4));
  mem_free(sgpChatSlotBuf, aGluChatSlotCpp, 0x8DA);
  timer_remove_node(pDlg, 0x1C);
  timer_remove_node(pDlg, 0x1D);
}

// 0x451940, 184 B. Count the human players (type 2) on team nArg0; the
// eight-record loop is fully unrolled.
int sub_451940(int nArg0) {
  int nCount = 0;
  for (int i = 0; i < 8; ++i) {
    if (sgNetSlots[i].bTeam == nArg0 && sgNetSlots[i].bType == 2) {
      nCount++;
    }
  }
  return nCount;
}

// 0x451A00, 162 B. Count the occupied slots, one per distinct team when
// teams are in play, and answer 0 when no human is among them. Declaring
// nHumans before nOthers is what gives the oracle its edi/ebx roles.
int CountSides() {
  int nHumans = 0;
  int nOthers = 0;
  int nTopTeam = 0;
  if (IsTVBGame() && gpLoadGameFile != 0) {
    return 2;
  }
  for (int i = 0; i < 8; ++i) {
    switch (sgNetSlots[i].bType) {
      case 1:
      case 5:
        if (IsTeamGame() || IsTVBGame()) {
          if (sgNetSlots[i].bTeam > nTopTeam) {
            nOthers++;
            nTopTeam = sgNetSlots[i].bTeam;
          }
        } else {
          nOthers++;
        }
        break;
      case 2:
        if (IsTeamGame() || IsTVBGame()) {
          if (sgNetSlots[i].bTeam > nTopTeam) {
            nHumans++;
            nTopTeam = sgNetSlots[i].bTeam;
          }
        } else {
          nHumans++;
        }
        break;
    }
  }
  if (nHumans >= 1) {
    return nHumans + nOthers;
  }
  return 0;
}

// 0x451AB0, 21 B. Unsuppress the slot controls and mark a refresh in progress.
void sub_451AB0() {
  sgdwSlotsHidden = 0;
  sgdwRefreshing = 1;
}

// 0x451AD0, 118 B. Gray every control of every row and freeze the panel.
//
// Four controls per row, walked as a pNext chain: the oracle keeps the cursor in ESI and
// reloads it after each call, and the last of the four is `ctrl_gray(pCtrl->pNext)` with
// no store back. The `lea edx, dword ptr [ebx+1Ch]` is the optimiser building 0x1C out of
// the loop counter, which is 0 when the loop falls out; the source says 0x1C.
//
// The loop cursor is a pointer to the one field the loop touches, biased to
// 0x6681B0 + 0xAC, because that is where the oracle emits its initialiser: walking a
// plain `chatslot_t *` strength-reduces to the same induction variable but sets it up
// after the counter rather than before, three bytes apart and nothing else differing.
// Only making the biased pointer a source variable moves it, since it is then scheduled
// in source position instead of being created by strength reduction.
void sub_451AD0(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  combo_reset_slider(reinterpret_cast<lbox_t *>(sgpCurrentCombo_g));
  img_update();
  ctrl_t **ppRow = &sgChatSlots_g[0].pCtrl;
  int nLeft = 12;  // 12 rows; ppRow steps a whole record each time
  do {
    ctrl_t *pCtrl = *ppRow;
    ctrl_gray(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_gray(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_gray(pCtrl);
    ctrl_gray(pCtrl->pNext);
    ppRow += sizeof(chatslot_t) / sizeof(ctrl_t *);
  } while (--nLeft);
  timer_remove_node(pDlg, 0x1C);
  timer_remove_node(pDlg, 0x1D);
  sgdwSlotsFrozen = 1;
}

// 0x451B50, 535 B. Lay the players out over the slot rows, at most nArg1 of
// them, with a team header row before each new team, then clear the rest.
// Each case ends with its own row store; the read counts are what give the
// oracle's register rotation, and the clear loop stores nPlayer first so its
// cursor is biased at nPlayer.
void sub_451B50(intptr_t nArg0, int nArg1) {
  chatslot_t *p = sgChatSlots_g;
  BYTE bTeam = 0;
  int nTeamArg = 0;
  for (int i = 0; i < 8; i++) {
    DWORD dwId = gPlayerData[i].dwPlayerID;
    if (gPlayback != 0) {
      switch (gPlayerData[i].nType) {
      case 2:
        if (nArg1-- == 0) {
          goto done;
        }
        sub_452F70(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      case 8:
        if (nArg1-- == 0) {
          goto done;
        }
        sub_452D60(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0, 0, nTeamArg);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      default:
        if (nArg1-- == 0) {
          goto done;
        }
        gPlayerData[i].nType = 6;
        sub_452D60(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0, 1, nTeamArg);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      }
      continue;
    }
    if (gPlayerData[i].nTeam != 0 && gPlayerData[i].nTeam > bTeam) {
      int bOpen = 0;
      switch (gPlayerData[i].nType) {
      case 1:
      case 5:
        bOpen = 1;
      case 2:
      case 6:
      case 8:
      case 9:
        bTeam = gPlayerData[i].nTeam;
        if (IsTeamGame()) {
          nTeamArg = IsNonPlayerTeam(bTeam);
        }
        sub_4531C0(reinterpret_cast<intptr_t>(p), bTeam, i, bOpen);
        p->nPlayer = 8;
        p->dwPlayerId = 0xFFFFFFFF;
        ++p;
        break;
      }
    }
    switch (gPlayerData[i].nType) {
    case 2:
      if (nArg1-- == 0) {
        goto done;
      }
      sub_452F70(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0);
      p->nPlayer = i;
      p->dwPlayerId = gPlayerData[i].dwStormID;
      ++p;
      break;
    case 5:
      if (!ComputersAllowed()) {
        continue;
      }
      if (nArg1-- == 0) {
        goto done;
      }
      sub_452BE0(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0);
      p->nPlayer = i;
      p->dwPlayerId = gPlayerData[i].dwStormID;
      ++p;
      break;
    case 6:
      if (nArg1-- == 0) {
        goto done;
      }
      sub_452D60(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0, 1, nTeamArg);
      p->nPlayer = i;
      p->dwPlayerId = gPlayerData[i].dwStormID;
      ++p;
      break;
    case 8:
      if (nArg1-- == 0) {
        goto done;
      }
      sub_452D60(reinterpret_cast<intptr_t>(p), dwId, bTeam, 0, 0, nTeamArg);
      p->nPlayer = i;
      p->dwPlayerId = gPlayerData[i].dwStormID;
      ++p;
      break;
    default:
      continue;
    }
  }
done:
  for (; p <= &sgChatSlots_g[11]; ++p) {
    p->dwState = 0;
    ctrl_t *pCtrl = p->pCtrl;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    p->nPlayer = 0;
    p->dwPlayerId = gPlayerData[0].dwStormID;
  }
}

// 0x451DA0, 158 B. Rebuild the whole slot panel.
//
// The slot-count select is one short-circuited `||`: the oracle loads the default
// unconditionally, between the second compare and its branch, and overwrites it on either
// true arm, which an if/else chain does not give. gpLoadGameFile is tested at the top and
// again at the bottom, re-read rather than cached, so those are two reads in source too.
void sub_451DA0(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  int nSlots;
  if (gpLoadGameFile != 0 || gPlayback != 0) {
    nSlots = 8;
  } else {
    nSlots = sgbSlotCount;
  }
  if (IsScenarioGame(NULL) && gPlayback == 0) {
    sub_451E40(reinterpret_cast<intptr_t>(pDlg), 8);
  } else {
    sub_451B50(reinterpret_cast<intptr_t>(pDlg), nSlots);
  }
  if (sgdwRefreshPending != 0) {
    sgdwRefreshPending = 0;
    sgdwRefreshing = 1;
    sub_451B50(reinterpret_cast<intptr_t>(pDlg), nSlots);
  }
  sub_453CC0();
  if (gpLoadGameFile != 0 || sgdwSlotsFrozen != 0) {
    sub_451AD0(reinterpret_cast<intptr_t>(pDlg));
  }
  sgdwRefreshing = 0;
}

// 0x451E40, 595 B. sub_451B50 for force games: for each of the four forces a
// header row named for the force, then that force's players, at most nArg1
// of them. Type 8 takes a row without spending the budget, as in the oracle.
// dwPlayerID is read once and each case ends with its own row store; the
// read counts are what bias the record cursor at dwStormID, as the oracle does.
void sub_451E40(intptr_t nArg0, int nArg1) {
  dword_668A20 = GetPlayerForce(g_LocalNationID);
  chatslot_t *p = sgChatSlots_g;
  for (int nForce = 1; nForce <= 4; nForce++) {
    int bHeader = 0;
    for (int i = 0; i < 8; i++) {
      if (GetPlayerForce(i) != nForce) {
        continue;
      }
      if (!bHeader) {
        switch (gPlayerData[i].nType) {
        case 2:
        case 5:
        case 6:
        case 8:
        case 9: {
          char szForce[0x1E];
          GetPlayerForceName(i, szForce, 0x1E);
          get_printable_string(szForce, szForce, 0x1E);
          p->b70 = static_cast<BYTE>(nForce);
          p->dwState = 2;
          ctrl_t *pCtrl = p->pCtrl;
          SStrCopy(p->szName, szForce, 0x41);
          sub_453A50(reinterpret_cast<intptr_t>(pCtrl), reinterpret_cast<intptr_t>(p->szName));
          pCtrl = pCtrl->pNext;
          ctrl_w24(pCtrl) &= 0xFEFF;
          ctrl_hide(pCtrl);
          pCtrl = pCtrl->pNext;
          ctrl_w24(pCtrl) &= 0xFEFF;
          ctrl_hide(pCtrl);
          p->nPlayer = i;
          p->dwPlayerId = gPlayerData[i].dwStormID;
          ++p;
          bHeader = 1;
          break;
        }
        }
      }
      DWORD dwId = gPlayerData[i].dwPlayerID;
      switch (gPlayerData[i].nType) {
      case 2:
        if (nArg1-- == 0) {
          goto done;
        }
        sub_452F70(reinterpret_cast<intptr_t>(p), dwId, 0, nForce);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      case 5:
        if (nArg1-- == 0) {
          goto done;
        }
        sub_452BE0(reinterpret_cast<intptr_t>(p), dwId, 0, static_cast<BYTE>(nForce));
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      case 6:
        if (nArg1-- == 0) {
          goto done;
        }
        sub_452D60(reinterpret_cast<intptr_t>(p), dwId, 0, nForce, 1, 0);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      case 8:
        sub_452D60(reinterpret_cast<intptr_t>(p), dwId, 0, nForce, 0, 0);
        p->nPlayer = i;
        p->dwPlayerId = gPlayerData[i].dwStormID;
        ++p;
        break;
      }
    }
  }
done:
  for (; p <= &sgChatSlots_g[11]; ++p) {
    p->dwState = 0;
    ctrl_t *pCtrl = p->pCtrl;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    pCtrl = pCtrl->pNext;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
    p->nPlayer = 0;
    p->dwPlayerId = gPlayerData[0].dwStormID;
  }
}

// Size a control from its own top left corner.
static inline void slot_ctrl_size(ctrl_t *pCtrl, short nWidth, short nHeight) {
  pCtrl->rc.right = pCtrl->rc.left + nWidth;
  pCtrl->rc.bottom = pCtrl->rc.top + nHeight;
}
// 0x4520C0, 670 B. Set up the chat room's slot rows: clear the twelve slot
// records and the slot state, load the slot icons, then bind each row's four
// controls to its record and size them, the four column headers after them,
// and start the two slot timers.
void sub_4520C0(intptr_t nArg0) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  int i;

  memset(sgChatSlots_g, 0, sizeof(sgChatSlots_g));
  sgdwSlotsDeadline = 0;
  sgdwSlotsHidden = 1;
  sgdwSlotsFrozen = 0;
  memset(dword_6689F4, 0, sizeof(dword_6689F4));
  sgpChatSlotBuf = ReadGroupFile("glue\\chatroom\\iChat.grp", 0, aGluChatSlotCpp, 0x8F7);
  chatslot_t *p = sgChatSlots_g;
  int nLeft = 12;  // the oracle counts the rows down beside i
  for (i = 0; nLeft; i++, p++, nLeft--) {
    ctrl_t *pCtrl;
    bmp_t bmp;
    short x, y, w, h;

    p->b70 = 0xFF;
    pCtrl = ctrl_find_ndx(pDlg, i * 4 + 0x1E);
    ctrl_slot(pCtrl) = p;
    p->pCtrl = pCtrl;
    p->wX = pCtrl->rc.left;
    p->wY = pCtrl->rc.top;
    ctrl_w24(pCtrl) |= 0x100;
    if (!sgdwSlotsHidden)
      ctrl_show(pCtrl);
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(sub_453AE0);
    pCtrl->dwFlags = (pCtrl->dwFlags & ~0x20000010) | 0x400;
    pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453280);
    ctrl_set_update(pCtrl);

    pCtrl = pCtrl->pNext;
    ctrl_slot(pCtrl) = p;
    pCtrl->dwFlags |= 0x10;
    slot_ctrl_size(pCtrl, 0xA4, 0x19);
    pCtrl->wType = 0xD;
    pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453770);
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(draw_combo);
    ctrl_set_update(pCtrl);
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);

    pCtrl = pCtrl->pNext;
    ctrl_slot(pCtrl) = p;
    pCtrl->dwFlags |= 0x10;
    slot_ctrl_size(pCtrl, 0x64, 0x19);
    pCtrl->wType = 0xD;
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(draw_combo);
    pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_4539A0);
    ctrl_set_update(pCtrl);
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);

    pCtrl = pCtrl->pNext;
    GROUP *pGrp = reinterpret_cast<GROUP *>(sgpChatSlotBuf);
    ctrl_slot(pCtrl) = p;
    grp_to_bmpx(pGrp, 5, &bmp, &x, &y, &w, &h);
    pCtrl->rc.right = pCtrl->rc.left + bmp.nWidth * 6;
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(sub_452620);
    pCtrl->rc.bottom = pCtrl->rc.top + bmp.nHeight;
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
  }

  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, 0x19);
  ctrl_w24(pCtrl) &= 0xFEFF;
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453280);
  ctrl_hide(pCtrl);
  for (i = 0; i < 4; i++) {
    pCtrl = ctrl_find_ndx(pDlg, i + 0x1A);
    // The flag word is cleared between computing right and storing it.
    dlg_word_t wFlags = ctrl_w24(pCtrl) & 0xFEFF;
    pCtrl->rc.right = pCtrl->rc.left + 0xA0;
    ctrl_w24(pCtrl) = wFlags;
    pCtrl->rc.bottom = pCtrl->rc.top + 0x10;
    pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453770);
    pCtrl->wType = 0xD;
    pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(draw_combo);
    ctrl_hide(pCtrl);
  }
  timer_create(pDlg, 0x1C, 400, reinterpret_cast<PFN_TIMER_FN *>(sub_452A00));
  timer_create(pDlg, 0x1D, 400, reinterpret_cast<PFN_TIMER_FN *>(sub_452AB0));
}

// 0x452360, 51 B. Release the eight cached blocks at 0x668190. The loop's induction
// variable is a byte offset rather than an index, with the base folded into the addressing
// mode, and its `jb` is unsigned, which is what an unsigned counter gives after strength
// reduction -- a signed `int i` gives `jl`. SMemFree's owner tag is the .rdata literal at
// 0x4ED6C8, spelled as an object so the push is 5 bytes.
void sub_452360(intptr_t nArg0) {
  for (unsigned nIdx = 0; nIdx < 8; ++nIdx) {
    void *pBlock = dword_668190[nIdx];
    if (pBlock != 0) {
      SMemFree(pBlock, aGluChatSlotCpp, 0x948, 0);
      dword_668190[nIdx] = NULL;
    }
  }
}

// 0x4523A0, 25 B. Publish a slot value, and redraw only if it changed. ECX is 32 bits
// (`[ecx*4+6689F4h]`) and EDX 8 (`movzx eax, dl`), then a tail jmp to sub_453CC0. The
// compare's memory operand comes first, which is source order: the array read is the
// left-hand side.
void sub_4523A0(int nIndex, BYTE bValue) {
  if (dword_6689F4[nIndex] != bValue) {
    dword_6689F4[nIndex] = bValue;
    sub_453CC0();
  }
}

// 0x4523C0, 119 B. Hand a free row to a player, unless some row already claims
// them in a state that forbids it.
//
// The two loops over the same array have different shapes, both the oracle's: the first
// is a down-counter whose bound the optimiser builds out of the flag's own initial 1, the
// second an up-counting signed index, because its counter is the register the flag used
// and has to be re-zeroed anyway. The `lea ecx, dword ptr [ecx]` after the `jmp` is an
// alignment nop for the second loop head.
void sub_4523C0(int nArg0) {
  int bFree = 1;
  chatslot_t *p = sgChatSlots_g;
  int nLeft = 12;
  do {
    if (p->b54 == nArg0 && (p->dwState == 8 || p->dwState == 5)) {
      bFree = 0;
    }
    ++p;
  } while (--nLeft);
  if (bFree) {
    chatslot_t *q = sgChatSlots_g;
    for (int i = 0; i < 12; ++i) {
      if (q->dwState == 1 && q->b70 == nArg0) {
        ctrl_t *pCtrl = q->pCtrl;
        q->dw74 = 0;
        pCtrl->dwFlags &= 0x7FFFFFEF;
        ctrl_set_update(pCtrl);
        return;
      }
      ++q;
    }
  }
}

// 0x452440, 409 B. Compare a slot row with the player record it shows and
// report whether anything must be redrawn (*pbRedraw) and whether the row
// still has a pending change (*pbChanged). Cases 4 and 5 fall through.
// The pending flag goes through locals: over a literal-address global the
// shipped form will not if-convert the ternary, and its case table moves.
void sub_452440(intptr_t nArg0, int nState, int nPlayer, int *pbChanged, int *pbRedraw) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(nArg0);
  *pbRedraw = sgdwRefreshing;
  switch (nState) {
    case 4:
    case 5:
      if (_stricmp(sgNetSlots[nPlayer].szName, p->szName)) {
        *pbRedraw = 1;
      }
    case 6:
    case 8:
    case 9:
      if (p->b54 != LocalNetSlot().bTeam && p->dw74 != 0) {
        *pbRedraw = 1;
        p->dw74 = 0;
      }
      if (_stricmp(sgNetSlots[nPlayer].szName, p->szName)) {
        *pbRedraw = 1;
        p->dw68 = 0;
        BOOL bTvb = IsTVBGame();
        DWORD dwPend = sgdwRefreshPending;
        sgdwRefreshPending = bTvb ? 1 : dwPend;
      }
      if (p->dw68 != 0 && p->dw6C <= dword_672658) {
        *pbRedraw = 1;
        p->dw68 = 0;
        BOOL bTvb = IsTVBGame();
        DWORD dwPend = sgdwRefreshPending;
        sgdwRefreshPending = bTvb ? 1 : dwPend;
      }
      if (p->dw64 != 0) {
        if (p->dwState == nState || p->dwPrevState != nState || p->dw6C <= dword_672658) {
          *pbRedraw = 1;
          p->dw64 = 0;
        }
      } else if (p->dwState != nState) {
        *pbRedraw = 1;
      }
      if (p->dw74 != 0) {
        BYTE bRace = sgNetSlots[nPlayer].bRace;
        if (bRace == p->b70 || bRace != p->b71 || p->dw6C <= dword_672658) {
          *pbRedraw = 1;
          p->dw74 = 0;
        }
      } else if (sgNetSlots[nPlayer].bRace != p->b70) {
        *pbRedraw = 1;
      }
      break;
  }
  if (p->dw64 == 0 && p->dw68 == 0 && p->dw74 == 0) {
    *pbChanged = 0;
  } else {
    *pbChanged = 1;
  }
}

// 0x452600, 23 B. Raise a control's +0x24 flag and show it unless suppressed.
//
// `mov eax, 100h` with `or word ptr [ecx+24h], ax`, rather than a 16-bit immediate, is
// MSVC avoiding a length-changing prefix; it falls out of a plain `|= 0x100`. The
// `jz ctrl_show` is a conditional tail jump, what a trailing `if (cond) f(x);` in a void
// function lowers to, with ECX already holding the argument.
void sub_452600(void *pArg0) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  ctrl_w24(pCtrl) |= 0x100;
  if (sgdwSlotsHidden == 0) {
    ctrl_show(pCtrl);
  }
}

// 0x452620, 171 B. The latency cell's PFN_CTRL_DRAW: up to six bars, one
// per 200 of latency, frame 5, 6 or 7 by count. The clamp is the ternary
// spelling; an `if` gives a branch where the oracle has `cmovl`.
void sub_452620(void *pArg0, int nArg1, int nArg2, int nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  DWORD dwLatency = ctrl_slot(pCtrl)->dwLatency;
  if (dwLatency == 0) {
    return;
  }
  int nBars = dwLatency / 200;
  nBars = nBars < 6 ? nBars : 6;
  int nFrame;
  if (nBars <= 2) {
    nFrame = 5;
  } else {
    nFrame = (nBars > 3) + 6;
  }
  int nX = 0;
  for (; nBars != 0; --nBars) {
    bmp_t bmp;
    rect16_t rc;
    short nOfsX, nOfsY, nWidth, nHeight;
    rc = pCtrl->rc;
    rc.left += nX;
    grp_to_bmpx(reinterpret_cast<GROUP *>(sgpChatSlotBuf), static_cast<unsigned char>(nFrame), &bmp, &nOfsX, &nOfsY, &nWidth, &nHeight);
    draw_bmp_effect(&bmp, 0, 0, &rc, 0x2000);
    nX += bmp.nWidth;
  }
}

// 0x4526D0, 684 B. Build the text for the nIdx-th player row: walk the slot
// rows, counting player slots, and for the row that lands on nIdx format its
// caption into a fresh Storm block cached at dword_668190[nIdx]. Returns the
// block, or NULL. Each case advances nSlot to the next legal player slot.
char *sub_4526D0(int nIdx, int nStrOfs) {
  char szText[0x100];
  char *pszText = NULL;

  if (dword_668190[nIdx]) {
    SMemFree(dword_668190[nIdx], aGluChatSlotCpp, 0x136, 0);
    dword_668190[nIdx] = NULL;
  }
  int nSlot = 0;
  for (int i = 0; i < 12; i++) {
    if (nSlot == 8)
      break;
    switch (sgChatSlots_g[i].dwState) {
      case 4:
      case 5:
        if (nSlot == nIdx) {
          psnprintf(szText, sizeof(szText), glues_get_str(0xAF), sgNetSlots[nSlot].szName);
          dword_668190[nIdx] = SMemAlloc((DWORD)(strlen(szText) + 1), aGluChatSlotCpp, 0x148, 0);
          SStrCopy(static_cast<char *>(dword_668190[nIdx]), szText, 0x7FFFFFFF);
          pszText = static_cast<char *>(dword_668190[nIdx]);
        }
        while (nSlot < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[++nSlot].nType))
          ;
        break;
      case 8:
        if (nSlot == nIdx) {
          psnprintf(szText, sizeof(szText), glues_get_str(0xB0), glues_get_str(nStrOfs + 0xAC));
          dword_668190[nIdx] = SMemAlloc((DWORD)(strlen(szText) + 1), aGluChatSlotCpp, 0x157, 0);
          SStrCopy(static_cast<char *>(dword_668190[nIdx]), szText, 0x7FFFFFFF);
          pszText = static_cast<char *>(dword_668190[nIdx]);
        }
        while (nSlot < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[++nSlot].nType))
          ;
        break;
      case 6:
        if (nSlot == nIdx) {
          psnprintf(szText, sizeof(szText), glues_get_str(0xAF), glues_get_str(0x84));
          dword_668190[nIdx] = SMemAlloc((DWORD)(strlen(szText) + 1), aGluChatSlotCpp, 0x163, 0);
          SStrCopy(static_cast<char *>(dword_668190[nIdx]), szText, 0x7FFFFFFF);
          pszText = static_cast<char *>(dword_668190[nIdx]);
        }
        while (nSlot < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[++nSlot].nType))
          ;
        break;
      case 9:
        while (nSlot < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[++nSlot].nType))
          ;
        break;
    }
    if (pszText)
      break;
  }
  return pszText;
}

// 0x4529A0, 83 B. Mark every row in a given state as selected or not. The
// two ctrl_set_update calls are tail-merged by MSVC into the oracle's one.
// The loop is bounded by the pointer so that MSVC's own down-counter is
// initialised after the cursor, as in the oracle.
void sub_4529A0(int nArg0, int nArg1) {
  for (chatslot_t *p = sgChatSlots_g; p < &sgChatSlots_g[12]; ++p) {
    if (p->dwState == static_cast<DWORD>(nArg0)) {
      ctrl_t *pCtrl = p->pCtrl;
      if (p->b70 == nArg1) {
        pCtrl->dwFlags |= 0x80000000;
        ctrl_set_update(pCtrl);
      } else if (p->dw74 != 0) {
        pCtrl->dwFlags &= 0x7FFFFFFF;
        ctrl_set_update(pCtrl);
      }
    }
  }
}

// 0x452A00, 163 B. Poll each occupied row's player caps and show the
// latency cell while the value is non-zero. The loop is bounded by the
// pointer so the cursor init precedes the counter (see sub_4529A0).
void sub_452A00() {
  playercaps_t caps;
  caps.dwSize = sizeof(caps);
  for (chatslot_t *p = sgChatSlots_g; p < &sgChatSlots_g[12]; ++p) {
    ctrl_t *pCtrl = p->pCtrl->pNext->pNext->pNext;
    if (p->dwState == 4) {
      if (SNetGetPlayerCaps(p->dwPlayerId, &caps)) {
        DWORD dwLatency = caps.dwLatency;
        p->dwLatency = dwLatency;
        if (dwLatency != 0) {
          ctrl_w24(pCtrl) |= 0x100;
          if (sgdwSlotsHidden == 0) {
            ctrl_show(pCtrl);
          }
        } else {
          ctrl_w24(pCtrl) &= 0xFEFF;
          ctrl_hide(pCtrl);
        }
        ctrl_set_update(pCtrl);
      }
    } else {
      ctrl_w24(pCtrl) &= 0xFEFF;
      ctrl_hide(pCtrl);
    }
  }
}

// 0x452AB0, 299 B. League rows: ask for each named player's stats once, and
// mark rows whose answer came back. sub_452600 is a real call here, hence
// its noinline declaration.
void sub_452AB0() {
  char szCmd[0x49];
  if (league_get_name() == 0) {
    return;
  }
  for (chatslot_t *p = sgChatSlots_g; p < &sgChatSlots_g[12]; ++p) {
    DWORD dwState = p->dwState;
    if (dwState == 8 || dwState == 9 || dwState == 6 || dwState == 7) {
      if (p->dwA4 == 1 || p->dwA0 == 1) {
        p->dwA4 = 0;
        p->dwA0 = 0;
      }
      ctrl_t *pCtrl = p->pCtrl;
      if (pCtrl->pData != 0) {
        ctrl_w24(pCtrl) &= 0xFEFF;
        ctrl_hide(pCtrl);
        p->pCtrl->pData = 0;
      }
    } else {
      DWORD dwVal = dword_6689F4[p->dwPlayerId];
      if (dwVal == 100 || dwVal == 0) {
        if (p->szName[0] != 0 && p->dwA4 == 0) {
          SStrPrintf(szCmd, 0x49, "/stats %s", p->szName);
          SNetSendLeagueCommand(szCmd, reinterpret_cast<void *>(sub_451830));
          p->dwA4 = 1;
        } else if (p->dwA0 != 0) {
          void *pData = p->pCtrl->pData;
          if (pData == 0 || SStrCmp(reinterpret_cast<const char *>(pData), "  L", 3) != 0) {
            sub_452600(p->pCtrl);
            ctrl_set_text(p->pCtrl, const_cast<char *>("  L"));
          }
        }
      }
    }
  }
}

// 0x452BE0, 378 B. Put a row into state 6 for a player. The team flag is
// bRedraw itself; MSVC copies it into the dead nArg2 slot at the join.
void sub_452BE0(intptr_t nArg0, int nArg1, int nArg2, BYTE bArg3) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(nArg0);
  int nPlayer = nArg1;
  int bRedraw;
  int bChanged;
  sub_452440(nArg0, 6, nPlayer, &bChanged, &bRedraw);
  int nLocalTeam = LocalNetSlot().bTeam;
  if (p->dw58 != 0) {
    if (nLocalTeam != nArg2) {
      p->dw58 = 0;
      bRedraw = 1;
    }
  } else if (nLocalTeam == nArg2) {
    p->dw58 = 1;
    bRedraw = 1;
  }
  if (bChanged == 0) {
    p->dwState = 6;
    p->b54 = static_cast<BYTE>(nArg2);
    p->dw58 = LocalNetSlot().bTeam == nArg2;
    p->b5C = bArg3;
    p->szName[0] = 0;
    p->b70 = sgNetSlots[nPlayer].bRace;
  }
  ctrl_t *pCtrl = p->pCtrl;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
  pCtrl = pCtrl->pNext;
  if (bRedraw == 0 && bChanged == 0) {
    return;
  }
  sub_4532B0(reinterpret_cast<intptr_t>(pCtrl), 0, 0, 0, sgNetSlots[nPlayer].bTeam, 4);
  if (!IsGameCreator(aGluChatSlotCpp, 0x59A) || IsScenarioGame(NULL) || bChanged) {
    ctrl_gray(pCtrl);
  }
  pCtrl = pCtrl->pNext;
  sub_4538D0(reinterpret_cast<intptr_t>(pCtrl), p->b70);
  if (!IsGameCreator(aGluChatSlotCpp, 0x5A1) || IsScenarioGame(NULL) || bChanged) {
    ctrl_gray(pCtrl);
  }
  if (gPlayback != 0) {
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
  }
  pCtrl = pCtrl->pNext;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
}

// Set or clear the row's 0x4000 flag bit to match bSet; nonzero when it
// changed. Inlined into sub_452D60, where it is what gives the team argument
// EBX and leaves the redraw flag in the team's dead stack slot.
static __forceinline int oc_slot_a8_flag(chatslot_t *p, int bSet)
{
  if (bSet != 0) {
    if ((p->dwA8 & 0x4000) == 0) {
      p->dwA8 |= 0x4000;
      return 1;
    }
  } else if (p->dwA8 & 0x4000) {
    p->dwA8 &= ~0x4000;
    return 1;
  }
  return 0;
}

// 0x452D60, 516 B. sub_452BE0's sibling for states 8 and 9, with the
// +0xA8 flag bit and the type/race cells. nArg3 is an int read for its low
// byte: as a BYTE parameter its dead slot takes the spilled slot offset that
// the oracle keeps in nArg4's.
void sub_452D60(intptr_t nArg0, int nArg1, int nArg2, int nArg3, int nArg4, int nArg5) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(nArg0);
  int nPlayer = nArg1;
  int bRedraw;
  int bChanged;
  int nState = nArg4 ? 8 : 9;
  sub_452440(nArg0, nState, nPlayer, &bChanged, &bRedraw);
  int nLocalTeam = LocalNetSlot().bTeam;
  if (p->dw58 != 0) {
    if (nLocalTeam != nArg2) {
      p->dw58 = 0;
      bRedraw = 1;
    }
  } else if (nLocalTeam == nArg2) {
    p->dw58 = 1;
    bRedraw = 1;
  }
  if (bChanged == 0) {
    p->dwState = nState;
    p->b54 = static_cast<BYTE>(nArg2);
    p->dw58 = LocalNetSlot().bTeam == nArg2;
    p->b5C = static_cast<BYTE>(nArg3);
    p->szName[0] = 0;
    p->b70 = sgNetSlots[nPlayer].bRace;
  }
  if (oc_slot_a8_flag(p, nArg5))
    bRedraw = 1;
  ctrl_t *pCtrl = p->pCtrl;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
  pCtrl = pCtrl->pNext;
  if (nArg4 == 0 && IsTeamGame()) {
    sub_4523C0(nArg2);
  }
  if (bRedraw == 0 && bChanged == 0) {
    return;
  }
  sub_4532B0(reinterpret_cast<intptr_t>(pCtrl), 0, 0, 0, sgNetSlots[nPlayer].bTeam, nArg4 ? 2 : 3);
  if (!IsGameCreator(aGluChatSlotCpp, 0x5F3) || sgNetSlots[nPlayer].bType == 9 || bChanged) {
    ctrl_gray(pCtrl);
  }
  if (!IsTeamGame() || nArg5 != 0) {
    pCtrl = pCtrl->pNext;
    ctrl_gray(pCtrl);
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
  } else {
    pCtrl = pCtrl->pNext;
    sub_4538D0(reinterpret_cast<intptr_t>(pCtrl), p->b70);
    if (sgNetSlots[nPlayer].bTeam != LocalNetSlot().bTeam || bChanged) {
      ctrl_gray(pCtrl);
    }
  }
  if (gPlayback != 0) {
    ctrl_w24(pCtrl) &= 0xFEFF;
    ctrl_hide(pCtrl);
  }
  pCtrl = pCtrl->pNext;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
}

// 0x452F70, 583 B. Put a row into state 4 (another player) or 5 (the local
// player) and refresh its name, race and gray state. The two gray tests are
// both in the source; each calls IsScenarioGame again.
void sub_452F70(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(nArg0);
  int nPlayer = nArg1;
  int bRedraw;
  int bChanged;
  int nState = (nPlayer == g_LocalNationID) + 4;
  sub_452440(nArg0, nState, nPlayer, &bChanged, &bRedraw);
  int nLocalTeam = LocalNetSlot().bTeam;
  if (p->dw58 != 0) {
    if (nLocalTeam != nArg2) {
      p->dw58 = 0;
      bRedraw = 1;
    }
  } else if (nLocalTeam == nArg2) {
    p->dw58 = 1;
    bRedraw = 1;
  }
  if (bChanged == 0) {
    p->dwState = nState;
    p->b54 = static_cast<BYTE>(nArg2);
    p->dw58 = LocalNetSlot().bTeam == nArg2;
    p->b5C = static_cast<BYTE>(nArg3);
    p->b70 = sgNetSlots[nPlayer].bRace;
  }
  ctrl_t *pCtrl = p->pCtrl;
  ctrl_w24(pCtrl) |= 0x100;
  if (sgdwSlotsHidden == 0) {
    ctrl_show(pCtrl);
  }
  pCtrl = pCtrl->pNext;
  if (IsTeamGame()) {
    sub_4523C0(nArg2);
  }
  if (bRedraw != 0 || bChanged != 0) {
    SStrCopy(p->szName, sgNetSlots[nPlayer].szName, 0x41);
    sub_4532B0(reinterpret_cast<intptr_t>(pCtrl), reinterpret_cast<intptr_t>(p->szName),
               nPlayer == g_LocalNationID, 0, sgNetSlots[nPlayer].bTeam, 0);
    if (bChanged) {
      ctrl_gray(pCtrl);
    }
    pCtrl = pCtrl->pNext;
    sub_4538D0(reinterpret_cast<intptr_t>(pCtrl), p->b70);
    if (!(((p->dwState == 5 ||
            (IsTeamGame() && sgNetSlots[nPlayer].bTeam == LocalNetSlot().bTeam)) &&
           (!IsScenarioGame(NULL) || is_race_selectable(static_cast<BYTE>(nPlayer))) && !bChanged) ||
          (p->dwState == 5 &&
           (!IsScenarioGame(NULL) || is_race_selectable(static_cast<BYTE>(nPlayer))) && !bChanged))) {
      ctrl_gray(pCtrl);
    }
    if (gPlayback != 0) {
      ctrl_w24(pCtrl) &= 0xFEFF;
      ctrl_hide(pCtrl);
    }
  }
  if (IsScenarioGame(NULL) && nArg3 == dword_668A20 && sgdwSlotsDeadline <= dword_672658) {
    sub_4529A0(2, nArg3);
  }
  if (IsTeamGame() && nPlayer == g_LocalNationID && sgdwSlotsDeadline <= dword_672658) {
    sub_4529A0(1, sgNetSlots[nPlayer].bTeam);
  }
}

// 0x4531C0, 183 B. Turn a row into an open team slot named for the team.
void sub_4531C0(intptr_t nArg0, int nArg1, int nArg2, int nArg3) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(nArg0);
  ctrl_t *pCtrl = p->pCtrl;
  p->dwState = 1;
  p->b70 = static_cast<BYTE>(nArg1);
  if (IsTeamGame()) {
    strcpy(p->szName, GetTeamName(static_cast<BYTE>(nArg1)));
  }
  if (IsTVBGame()) {
    strcpy(p->szName, glues_get_str(nArg1 + 0xAC));
  }
  sub_453A50(reinterpret_cast<intptr_t>(pCtrl), reinterpret_cast<intptr_t>(p->szName));
  if (nArg3 != 0) {
    p->dw74 = 0;
    if (!IsTVBGame()) {
      pCtrl->dwFlags &= 0xDFFFFFEF;
    }
  } else {
    p->dw74 = 1;
  }
  pCtrl = pCtrl->pNext;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
  pCtrl = pCtrl->pNext;
  ctrl_w24(pCtrl) &= 0xFEFF;
  ctrl_hide(pCtrl);
}

// 0x453280, 38 B. The slot row's own control handler.
//
// The family's core shape without the register spill: the tag test tail-jumps to the base
// handler, and event 0x0D is swallowed only while the +0x24 flag is clear -- the `jnz`
// goes to the base handler, so the set case is the one that falls through to ctrl_evn_dlg.
int sub_453280(void *pArg0, void *pArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);
  if (pEvent->wTag != 0x0E) {
    return ctrl_text(pCtrl, pEvent);
  }
  if (pEvent->dwId == 0x0D && (ctrl_w24(pCtrl) & 0x100) == 0) {
    return 1;
  }
  return ctrl_evn_dlg(pCtrl, pEvent);
}

// 0x4532B0, 1178 B. Fill a slot row's type combo with the slot types the game allows.
void sub_4532B0(intptr_t nArg0, intptr_t nArg1, int nArg2, int nArg3, BYTE bArg4, int nArg5) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pCtrl);
  const char *pszName = reinterpret_cast<const char *>(nArg1);
  chatslot_t *pSlot;
  BYTE bSel;
  int bTvb;
  int anTeamFirst[2];
  char abTeam[8];
  char abSlotIdx[8];
  int bFirst;
  int bFound;
  int k;

  ctrl_w24(pCtrl) |= 0x100;
  if (sgdwSlotsHidden == 0) {
    ctrl_show(pCtrl);
  }
  ctrl_ungray(pCtrl);
  ctrl_set_update(pCtrl);
  ctrl_send_msg(pCtrl, 6, 0);
  pSlot = ctrl_slot(pCtrl);
  bSel = 0xFF;
  if (IsTVBGame() && gpLoadGameFile == NULL) {
    bTvb = 1;
    anTeamFirst[0] = -1;
    anTeamFirst[1] = -1;
    if (gPlayback == 0) {
      int i, nTeam1, nCount;
      char *pIdx;
      memset(abTeam, -1, sizeof(abTeam));
      memset(abSlotIdx, -1, sizeof(abSlotIdx));
      pIdx = abSlotIdx;
      for (i = 0; i < 8; i++) {
        if (IsLegalChatroomSlotVal(gNetPlayerData_g[i].nType))
          *pIdx++ = static_cast<char>(i);
      }
      nTeam1 = gametype_get_subtype();
      i = 0;
      nCount = 0;
      while (nCount < nTeam1 && i < 8) {
        if (IsLegalChatroomSlotVal(gNetPlayerData_g[i].nType)) {
          abTeam[i] = 1;
          nCount++;
        }
        i++;
      }
      for (; i < 8; i++) {
        if (IsLegalChatroomSlotVal(gNetPlayerData_g[i].nType))
          abTeam[i] = 2;
      }
      for (i = 0; i < nTeam1; i++)
        abTeam[i] = 1;
      for (i = nTeam1; i < 8; i++)
        abTeam[i] = 2;
      i = 0;
      for (int j = 0; j < 12; j++) {
        if (i == 8)
          break;
        switch (sgChatSlots_g[j].dwState) {
        case 4:
        case 5:
        case 6:
        case 9:
          while (i < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[++i].nType)) {
          }
          break;
        case 8:
          while (i < 8 && !IsLegalChatroomSlotVal(gNetPlayerData_g[i].nType))
            i++;
          if (anTeamFirst[abTeam[i] - 1] < 0)
            anTeamFirst[abTeam[i] - 1] = i;
          i++;
          break;
        }
      }
    }
  } else {
    bTvb = 0;
    anTeamFirst[0] = -1;
    anTeamFirst[1] = -1;
  }
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(lbox_ctrl_combo);
  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  bFound = 0;
  bFirst = 0;
  const slottypeopt_t *pOpt = stru_503B68;
  for (k = 0; k < 13; k++, pOpt++) {
    const char *pszText;
    if (gPlayback == 0 && bTvb != 0) {
      if (pOpt->dwType == 6) {
        char nTeam;
        // The two || tests give the oracle's latch: k reloaded before pCtrl.
        if (gPlayerData[pSlot->nPlayer].dwStormID == 0 || pSlot->dwState != 4)
          continue;
        // Re-read, not a local: the extra dwStr uses bias the table pointer to +4.
        if (abSlotIdx[pOpt->dwStr] < 0)
          continue;
        nTeam = abTeam[abSlotIdx[pOpt->dwStr]];
        if (abTeam[pSlot->nPlayer] == nTeam || bFound)
          continue;
        bFirst = anTeamFirst[nTeam - 1] >= 0;
        if (bFirst && abSlotIdx[pOpt->dwStr] != anTeamFirst[nTeam - 1])
          continue;
      }
    } else if (pOpt->dwType == 6) {
      continue;
    }
    if (!pOpt->bIfArg2 && nArg2)
      continue;
    if (!pOpt->bIfNotArg2 && !nArg2)
      continue;
    if (pOpt->bNeedsName && !pszName)
      continue;
    if (nArg3 && !pOpt->bIfArg3)
      continue;
    if (pOpt->dwType == 4 && !ComputersAllowed())
      continue;
    if (pOpt->dwType == 4 && (gPlayback || (IsScenarioGame(NULL) && nArg5 != 4)))
      continue;
    if (IsTeamGame() && !pOpt->bTeamGame && !IsNonPlayerTeam(bArg4) && !IsComputerTeam(bArg4))
      continue;
    if (!pOpt->bPlayback && gPlayback)
      continue;
    switch (pOpt->dwType) {
    case 0:
      pszText = pszName;
      break;
    case 6:
      pszText = sub_4526D0(abSlotIdx[pOpt->dwStr], abTeam[abSlotIdx[pOpt->dwStr]]);
      break;
    default:
      pszText = glues_get_str(pOpt->dwStr);
      break;
    }
    if (pszText) {
      BYTE nItem = lbox_add_item(pCtrl, pszText, 0);
      if (nItem == 0xFF)
        break;
      if (pOpt->dwType != 6)
        lbox_set_data(pBox, nItem, reinterpret_cast<void *>((DWORD_PTR)pOpt->dwType));
      else
        lbox_set_data(pBox, nItem, reinterpret_cast<void *>((DWORD_PTR)(abSlotIdx[pOpt->dwStr] + pOpt->dwType)));
      if (pOpt->dwType == nArg5)
        bSel = nItem;
      if (bFirst)
        bFound = 1;
    }
  }
  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, bSel);
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453770);
  if (!IsGameCreator(aGluChatSlotCpp, 0x49B) || lbox_get_count(pBox) == 0)
    ctrl_gray(pCtrl);
}

// 0x453770, 74 B. The type combo's control handler.
//
// The dispatch is a switch: the chained subtracts off one live value are its lowering,
// where an if-chain on the same two ids gives two independent compares. The 0x0B arm's
// selection is a byte with 0xFF for "nothing selected", and it reaches sub_4537C0 as
// `mov dl, al`, where an `int` parameter would force a 3-byte `movzx edx, al`. The three
// arms that reach lbox_ctrl_combo are not one jump -- the tag mismatch goes straight in,
// the other two share a `jmp` -- which one trailing `return lbox_ctrl_combo(...)` gives.
int sub_453770(void *pArg0, void *pArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);
  if (pEvent->wTag != 0x0E) {
    return lbox_ctrl_combo(reinterpret_cast<lbox_t *>(pCtrl), pEvent);
  }
  switch (pEvent->dwId) {
    case 0x0B: {
      BYTE nSel = static_cast<BYTE>(pEvent->dwParam);
      if (nSel == 0xFF) {
        break;
      }
      if (sub_4537C0(reinterpret_cast<lbox_t *>(pCtrl), nSel)) {
        sub_451790();
      }
      return 1;
    }
    case 0x0D:
      if ((ctrl_w24(pCtrl) & 0x100) == 0) {
        return 1;
      }
      break;
  }
  return lbox_ctrl_combo(reinterpret_cast<lbox_t *>(pCtrl), pEvent);
}

// 0x4537C0, 220 B. Act on the slot type the combo selected. The case order
// is the oracle's block order (5 before 4); 6 has no arm.
BOOL sub_4537C0(lbox_t *pBox, unsigned char nItem) {
  chatslot_t *p = ctrl_slot(reinterpret_cast<ctrl_t *>(pBox));
  intptr_t nAction = reinterpret_cast<intptr_t>(lbox_get_data(pBox, nItem));
  switch (nAction) {
    case 0:
      return 1;
    case 1:
      if (sub_4BAF90()) {
        return 1;
      }
      break;
    case 2:
      if (sub_4BB290(p->nPlayer)) {
        sub_453C80(p, 8);
        return 1;
      }
      break;
    case 3:
      if (sub_4BB080(p->nPlayer)) {
        sub_453C80(p, 9);
        return 1;
      }
      break;
    case 5:
      if (BanPlayer(p->nPlayer)) {
        sub_453C80(p, 8);
        return 1;
      }
      break;
    case 4:
      if (sub_4BB0E0(p->nPlayer)) {
        sub_453C80(p, 6);
        return 1;
      }
      break;
    case 7:
    case 8:
    case 9:
    case 10:
    case 11:
    case 12:
    case 13:
      if (sub_4BB2F0(p->nPlayer, nAction - 6)) {
        sub_453B10(p);
        return 1;
      }
      break;
  }
  return 0;
}

// 0x4538D0, 194 B. Fill the race combo from stru_503D0C and select bArg1.
void sub_4538D0(intptr_t nArg0, BYTE bArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pCtrl);
  ctrl_w24(pCtrl) |= 0x100;
  if (sgdwSlotsHidden == 0) {
    ctrl_show(pCtrl);
  }
  ctrl_ungray(pCtrl);
  ctrl_set_update(pCtrl);
  ctrl_send_msg(pCtrl, 6, 0);
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(lbox_ctrl_combo);
  lbox_set_recalc(pBox, 0);
  lbox_remove_all(pBox);
  BYTE bSel = 0;
  for (int i = 0; i < 4; ++i) {
    BYTE nItem = static_cast<BYTE>(lbox_add_item(pCtrl, error_get_str(stru_503D0C[i].bStrId), 0));
    if (nItem == 0xFF) {
      break;
    }
    lbox_set_data(pBox, nItem, reinterpret_cast<void *>(stru_503D0C[i].bRace));
    if (stru_503D0C[i].bRace == bArg1) {
      bSel = nItem;
    }
  }
  lbox_set_recalc(pBox, 1);
  lbox_set_selection(pBox, bSel);
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_4539A0);
}

// 0x4539A0, 86 B. The race combo's control handler.
//
// sub_453770's switch with the control spilled: this body calls out three times on the
// 0x0B arm, so ECX cannot survive and `mov esi, ecx` earns its `push esi`, hoisted above
// the tag test, so the `pop esi` shows up on the mismatch arm too. The
// `cmp dl, byte ptr [esi+48h]` reads the listbox's current selection, so the guard is a
// new selection that is not the sentinel. lbox_get_data needs no `mov ecx` in front of it
// because ECX still holds the incoming control, which passing pCtrl by name reproduces.
int sub_4539A0(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag != 0x0E) {
    return lbox_ctrl_combo(reinterpret_cast<lbox_t *>(pCtrl), pEvent);
  }
  switch (pEvent->dwId) {
    case 0x0B: {
      BYTE nSel = static_cast<BYTE>(pEvent->dwParam);
      if (nSel != ctrl_b48(pCtrl) && nSel != 0xFF) {
        BYTE bRace = static_cast<BYTE>(reinterpret_cast<DWORD_PTR>(
            lbox_get_data(reinterpret_cast<lbox_t *>(pCtrl), nSel)));
        sub_453A00(pCtrl, bRace);
        ctrl_gray(pCtrl);
      }
      return 1;
    }
    case 0x0D:
      if ((ctrl_w24(pCtrl) & 0x100) == 0) {
        return 1;
      }
      break;
  }
  return lbox_ctrl_combo(reinterpret_cast<lbox_t *>(pCtrl), pEvent);
}

// 0x453A00, 78 B. Commit a race change on the row a control belongs to.
//
// bRace is a byte: the body never reads the argument wider than 8 bits (`mov bl, dl` on
// entry, `mov byte ptr [esi+70h], bl` at the end), and its caller sub_4539A0 hands the
// selection over with a 2-byte `mov dl, al` where an `int` parameter would force a
// `movzx`. CanChangeRace's EDX is never reloaded, since `mov bl, dl` left DL holding it.
// The trailing sub_451790 is a tail jump off the guarded arm while the other arm returns
// normally, so the two cannot be merged.
void sub_453A00(ctrl_t *pCtrl, BYTE bRace) {
  chatslot_t *p = ctrl_slot(pCtrl);
  if (CanChangeRace(p->bRace, bRace, g_LocalNationID)) {
    sub_4BAEA0(p->bRace, bRace);
    p->b71 = p->b70;
    p->b70 = bRace;
    p->dw74 = 1;
    p->dw6C = dword_672658 + sgdwSlotTimeout * 4;
    sub_451790();
  }
}

// 0x453A50, 129 B. Build one slot row's NAME cell.
//
// Both faces of the length-changing-prefix rule show up here: the 0xB8 addend is
// materialised in a register, a byte longer than the 16-bit immediate would be, while the
// sibling `add ax, 12h` keeps its imm8, which carries no penalty. Both fall out of the
// plain `rc.right = rc.left + 0xB8` and `rc.bottom = rc.top + 0x12`.
//
// The deadline compare is unsigned, which is what types that global; the guarded `and`
// clears bit 0 only once the tick counter has caught up. The two handler installs are
// DIR32 stores of this file's own bodies into ctrl_t +0x2A and +0x2E.
void sub_453A50(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_w24(pCtrl) |= 0x100;
  if (sgdwSlotsHidden == 0) {
    ctrl_show(pCtrl);
  }
  if (sgdwSlotsFrozen == 0) {
    ctrl_ungray(pCtrl);
  }
  pCtrl->rc.right = static_cast<short>(pCtrl->rc.left + 0xB8);
  pCtrl->rc.bottom = static_cast<short>(pCtrl->rc.top + 0x12);
  if (dword_672658 >= sgdwSlotsDeadline) {
    pCtrl->dwFlags &= 0x7FFFFFFE;
  }
  pCtrl->dwFlags |= 0x20000010;
  pCtrl->pfnEvt = reinterpret_cast<PFN_CTRL_EVT>(sub_453BC0);
  pCtrl->pfnDraw = reinterpret_cast<PFN_CTRL_DRAW>(sub_453B50);
  ctrl_set_text(pCtrl, reinterpret_cast<void *>(nArg1));
  ctrl_set_update(pCtrl);
}

// 0x453AE0, 42 B. The slot NAME cell's PFN_CTRL_DRAW.
//
// `ret 8h` with two register arguments and two stack words is the PFN_CTRL_DRAW contract;
// the two stack words are never read. The guard is an occupied row, state 4 or 5, with
// text in the control. Setting gbFontJust before the call is the draw_*text family's
// idiom: draw_ctrl_text saves and restores it itself.
void sub_453AE0(void *pArg0, int nArg1, int nArg2, int nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  DWORD dwState = ctrl_slot(pCtrl)->dwState;
  if ((dwState == 4 || dwState == 5) && pCtrl->pData != 0) {
    gbFontJust = 0x21;
    draw_ctrl_text(pCtrl, 0, 0, NULL);
  }
}

// 0x453B10, 52 B. Arm a row's refresh timer.
//
// The `cmovnz` means the store happens on both paths, so the shape is the ternary
// `g = cond ? 1 : g` rather than `if (cond) g = 1;`.
void sub_453B10(void *pArg0) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(pArg0);
  p->dw68 = 1;
  p->dw6C = dword_672658 + sgdwSlotTimeout * 4;
  sgdwRefreshPending = IsTVBGame() ? 1 : sgdwRefreshPending;
}

// 0x453B50, 103 B. The slot NAME cell's frame-and-text PFN_CTRL_DRAW.
//
// Both frame constants are materialised before the test and selected with a `cmovnz`, so
// the select is an expression rather than a branch, as in sub_453B10. draw_local_ctrl's
// seven arguments land as ECX = rc.left, EDX = rc.top, then five pushed right to left:
// the width `rc.right - rc.left + 1`, the theme metric gwUnk10_g, the frame id, this
// callback's own clip rectangle and 0x2000.
void sub_453B50(intptr_t nArg0, int nArg1, int nArg2, intptr_t nArg3) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  rect16_t *prcClip = reinterpret_cast<rect16_t *>(nArg3);
  dlg_dword_t dwFlags = pCtrl->dwFlags;
  int nFrame = 0x35;
  if (dwFlags & 0x80000080) {
    nFrame = 0x38;
  }
  if ((dwFlags & 2) == 0) {
    draw_local_ctrl(pCtrl->rc.left, pCtrl->rc.top,
                    pCtrl->rc.right - pCtrl->rc.left + 1, gwUnk10_g, nFrame,
                    prcClip, 0x2000);
  }
  if (pCtrl->pData != 0) {
    gbFontJust = 0x11;
    draw_ctrl_text(pCtrl, 4, 0, NULL);
  }
}

// 0x453BC0, 182 B. The slot row's PFN_CTRL_EVT for the per-state actions.
int sub_453BC0(void *pArg0, void *pArg1) {
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(pArg1);
  switch (pEvent->wTag) {
    case 4: {
      chatslot_t *p = ctrl_slot(pCtrl);
      switch (p->dwState) {
        case 1:
          if (IsTeamGame()) {
            sub_4BAEB0(p->b70);
            sub_4529A0(1, p->b70);
            sgdwSlotsDeadline = dword_672658 + sgdwSlotTimeout * 4;
          }
          IsTVBGame();
          break;
        case 2:
          sub_4BAE80(p->b70);
          sub_4529A0(2, p->b70);
          sgdwSlotsDeadline = dword_672658 + sgdwSlotTimeout * 4;
          break;
        case 3:
          nullsub___shared_();
          break;
      }
      return 1;
    }
    case 0x0E:
      if (pEvent->dwId == 0x0D && (ctrl_w24(pCtrl) & 0x100) == 0) {
        return 1;
      }
      return ctrl_evn_dlg(pCtrl, pEvent);
  }
  return ctrl_text(pCtrl, pEvent);
}

// 0x453C80, 59 B. Move a row into a new state and arm its timer.
//
// The same `cmovnz` tail as sub_453B10, reached from a body that also rolls the
// previous state forward -- `mov eax, dword ptr [ecx] / mov dword ptr [ecx+4h],
// eax` before the new state is stored, which is what makes +0x04 the previous
// value of +0x00 rather than an unrelated dword.
void sub_453C80(void *pArg0, int nArg1) {
  chatslot_t *p = reinterpret_cast<chatslot_t *>(pArg0);
  p->dwPrevState = p->dwState;
  p->dw64 = 1;
  p->dwState = nArg1;
  p->dw6C = dword_672658 + sgdwSlotTimeout * 4;
  sgdwRefreshPending = IsTVBGame() ? 1 : sgdwRefreshPending;
}

// 0x453CC0. Refreshes the value cell of every occupied row (state 4 or 5)
// from dword_6689F4, and hides the cell when the value is 0 or 100 and +0xA0
// is clear.
void sub_453CC0() {
  chatslot_t *p = sgChatSlots_g;
  // An up-count to 12 with `!=`; `<` or a down-count swaps two registers.
  for (int i = 0; i != 12; ++i, ++p) {
    if (p->dwState == 4 || p->dwState == 5) {
      DWORD dwVal = dword_6689F4[p->dwPlayerId];
      if (dwVal != 100 && dwVal != 0) {
        ctrl_t *pCtrl = p->pCtrl;
        ctrl_w24(pCtrl) |= 0x100;
        if (sgdwSlotsHidden == 0)
          ctrl_show(pCtrl);
        if (p->bValShown != dword_6689F4[p->dwPlayerId] ||
            p->pCtrl->pData != p->szValText) {
          p->bValShown = static_cast<BYTE>(dword_6689F4[p->dwPlayerId]);
          psnprintf(p->szValText, sizeof(p->szValText), "%3u",
                    dword_6689F4[p->dwPlayerId]);
          ctrl_set_text(p->pCtrl, p->szValText);
        }
      } else if (p->dwA0 == 0) {
        ctrl_t *pCtrl = p->pCtrl;
        ctrl_w24(pCtrl) &= ~0x100;
        ctrl_hide(pCtrl);
      }
    }
  }
}
