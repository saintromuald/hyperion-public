// select.h -- the unit-selection layer in src117/lang/select.cpp, oracle
// 0x49C140 to 0x49CFF0. Ten of this TU's rows are declared elsewhere: four in
// net_cmds.h, select_init in game.h, CUnit::sub_49C5A0 in CUnit.h, and four as
// prototypes in the .cpp files that call them.

#ifndef OC117_SELECT_H
#define OC117_SELECT_H

#include "platform.h"
#include "point16.h"
#include "pos.h"      // Hyperion: mpoint_t (stage E)

class CUnit;

// The formation context select_target builds on its stack and hands to
// select_form_calc_offset and CUnit::sub_49C5A0. The offsets and widths are read
// off those two bodies; the names are ours.
struct select_form_t {
  // Hyperion: the four points are of 4-byte positions on x64 (h/pos.h, stage E)
  /*0x00*/ mpoint_t  ptTarget;   // `mov eax, dword ptr [esi]` copied to +8/+0xC;
                                 // read signed, `movsx edx, word ptr [esi]`
  /*0x04*/ mpoint_t  ptUnit;     // CUnit::sub_49C5A0's only output; `lea edi,
                                 // dword ptr [ebx+4h]` then `mov [edi], eax`
  /*0x08*/ mpoint_t  ptCentre;   // read unsigned, `movzx ecx, word ptr [esi+8h]`
  /*0x0C*/ mpoint_t  ptClamped;  // `lea ecx, dword ptr [esi+0Ch]` into sub_49C210
  /*0x10*/ short     nOffsetX;   // `add ax, word ptr [ebx+10h]`, a 16-bit add
  /*0x12*/ short     nOffsetY;   // `add ax, word ptr [ebx+12h]`
  /*0x14*/ int       nArea;      // `add dword ptr [esi+14h], ecx`
  /*0x18*/ WORD      wSpread;    // `mov word ptr [esi+18h], ax`
  /*0x1A*/ BYTE      bFlags;     // `mov cl, byte ptr [ebx+1Ah]`; bits 1,2,3,4,8
};
OC_X86_LAYOUT(sizeof(select_form_t) == 0x1C,
              "select_form_t  select_target allocates it at [ebp-30h] and puts "
              "its next local at [ebp-14h], so it is at most 0x1C; the int at "
              "+0x14 makes 4 the alignment, so 0x1B rounds to exactly 0x1C");
static_assert(offsetof(select_form_t, ptTarget)  == 0x00, "select_form_t::ptTarget   `mov eax, dword ptr [esi]`");
OC_X86_LAYOUT(offsetof(select_form_t, ptUnit)    == 0x04, "select_form_t::ptUnit     `lea edi, dword ptr [ebx+4h]`");
OC_X86_LAYOUT(offsetof(select_form_t, ptCentre)  == 0x08, "select_form_t::ptCentre   `lea esi, dword ptr [ebx+8h]`");
OC_X86_LAYOUT(offsetof(select_form_t, ptClamped) == 0x0C, "select_form_t::ptClamped  `lea ecx, dword ptr [esi+0Ch]`");
OC_X86_LAYOUT(offsetof(select_form_t, nOffsetX)  == 0x10, "select_form_t::nOffsetX   `add ax, word ptr [ebx+10h]`");
OC_X86_LAYOUT(offsetof(select_form_t, nOffsetY)  == 0x12, "select_form_t::nOffsetY   `add ax, word ptr [ebx+12h]`");
OC_X86_LAYOUT(offsetof(select_form_t, nArea)     == 0x14, "select_form_t::nArea      `add dword ptr [esi+14h], ecx`");
OC_X86_LAYOUT(offsetof(select_form_t, wSpread)   == 0x18, "select_form_t::wSpread    `mov word ptr [esi+18h], ax`");
OC_X86_LAYOUT(offsetof(select_form_t, bFlags)    == 0x1A, "select_form_t::bFlags     `mov cl, byte ptr [ebx+1Ah]`");

// 0x49C140. Does pCandidate outrank pCurrent for the selection sound? A null
// pCurrent always loses. The result is 0 or 1.
int __fastcall sub_49C140(CUnit *pCurrent, CUnit *pCandidate);

// 0x49C210. Clamp a point to the map in pixels. The two fields are read `movzx`
// and compared `movsx` against WORD map bounds, which makes the point a
// point16_t and not a pair of ints.
void __fastcall sub_49C210(point16_t *pPos);

// 0x49C270. Both parameters are 16-bit: the body opens `mov si, dx` and compares
// `cmp si, cx`, and the path array it walks is WORD-wide too.
BOOL __fastcall sub_49C270(WORD wRegion, WORD wTarget);

// 0x49C2B0. Ally of the local player in a team game. The bound test is the signed
// `cmp esi, 8h / jnl`, so the player id is an int.
BOOL __fastcall sub_49C2B0(int nPlayer);

// 0x49C300. EDX is read 8 bits wide (`mov byte ptr [esp+0Bh], dl`).
void __fastcall select_form_calc_offset(select_form_t *pForm, BYTE bArg1);

// 0x49C7D0. Drop a player's whole selection.
void __fastcall sub_49C7D0(DWORD dwHumanID);

// Clear this unit from every selection it appears in. One ECX argument, a plain
// `ret`, and all three callers discard EAX.
void __fastcall sub_49C8B0(CUnit *pUnit);

// Seat a unit in a player's selection. `ret 4h`, so ECX, EDX and one stack word;
// two of the three callers consume the full EAX.
int __fastcall select_insert(CUnit *pUnit, DWORD dwHumanID, int nCount);

// 0x49C9D0. Close the gap pUnit leaves in nPlayer's selection and return what is
// left. The count comes back full-width in EAX.
int __fastcall select_fixup_units(CUnit *pUnit, int nPlayer);

// 0x49CAB0. Swap the staging list for ppUnits. The count is the signed
// `cmp esi, ebx / jl` bound.
void __fastcall sub_49CAB0(int nCount, CUnit **ppUnits);

// 0x49CF80. Commit a resolved unit list as the selection. The third parameter is
// a BYTE, which is how the oracle tests it (`cmp byte ptr [ebp+8h], 0h`); two
// .cpp files still spell their own prototype of it `int`.
void __fastcall select_units(int nCount, CUnit **ppUnits, BYTE bPlaySound,
                             int nSend);

// A callee of this TU that no other header of ours declares, and whose address
// is outside this TU's range; move it if a better-placed header wants it.

// 0x472210. Turns a failure code from the *Allowed predicates into a message,
// and is a different row from net_cmds.h's get_allowed_err @0x4B38A0, which
// reads the code. One int parameter in ECX, read full width, nothing pushed.
void __fastcall AllowedErr(int nErr);

#endif  // OC117_SELECT_H
