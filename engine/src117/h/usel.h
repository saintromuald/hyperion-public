// usel.h -- unit selection: the drag-rectangle lifecycle and the click and band
// target resolution that turns a screen point or rectangle into the player's
// selection. Oracle 0x4BBDD0-0x4BCF27, one contiguous TU.
//
// This file is also what keeps an `#include "usel.h"` resolving to our tree: the
// quarantined reference tree has a file of the same basename covering the same
// subsystem, and a name our generated code resolves has to be a name we own.

#ifndef OC117_USEL_H
#define OC117_USEL_H

#include "platform.h"
#include "point16.h"   // sgSelectStart
#include "rect16.h"    // sgSelectRect
#include "dialog.h"    // dlgevt_t -- the mouse handlers' argument
#include "cursor.h"    // ours: console_collision, reseated there w91
#include "selectlimits.h"  // SELECTION_MAX
#include "typelimits.h"    // weapon_id_t

class CUnit;   // `class`, not `struct` -- CUnit.h:133 declares it as a class and
               // mismatching the tag is a C4099 in the payload build.

// The selection cap of 12 is derived, not assumed: sub_4BC4C0 returns the literal
// 12 when its scan finds that many occupied slots, sub_4BC5B0 gates its overflow
// path on the same constant, and src117/h/statdraw.h's gpStatUnits -- the same
// selection this TU produces -- is CUnit *[12].
// Hyperion: 255 in a game of Hyperion's selection (stage S, selectlimits.h). The arrays
// these bodies fill hold USEL_SELECT_CAPACITY, room for 255 and the null that ends a list.
#define USEL_MAX_SELECT SelectionMax()
#define USEL_SELECT_CAPACITY (SELECTION_WIDE + 1)

// 0x4BC730. Non-zero when a unit type can never be band-selected. The parameter
// is 16-bit on call-site evidence: sub_4BC5B0 loads it with a plain move into CX,
// where passing a WORD to a wider parameter would zero-extend. The body is a
// switch over unit ids 0x0E..0xD0 through a 195-entry byte index table, whose
// default returns 1, and the result is consumed 32-bit.
int __fastcall usel_type_unselectable(WORD wType);

// 0x4BBE30. Invalidates the band rectangle and clears the dragging flag. It is
// never called: its only reference in the image is usel_drag_start storing it
// into gpfCancelDrag, so it exists as a function pointer. Declared nullary
// because the body reads no register argument.
void __fastcall usel_cancel_drag(void);

// 0x4BCCC0. usel_cancel_drag's body plus a tail `jmp game_reset_handlers`, and
// this is the one with real callers -- modeless_dlg_startup, pause_game and
// scroll_update. The two names read backwards from the intuition, the "_evt" one
// being the directly called one, but they are the ledger's.
void __fastcall usel_cancel_drag_evt(void);

// 0x4BCC40. Draws the band box, or tears it down when the mouse is no longer
// captured.
void __fastcall usel_draw_rect(void);

// 0x4BBDD0. The replacement policy at the selection cap: with the array full,
// overwrite the first slot whose occupant has a lower sprite draw priority than
// the candidate, never displacing pTarget. `ret 8h`, two register parameters and
// two stack ones, and ppSlot is a cursor into the selection array rather than its
// base, since sub_4BC4C0 passes an offset into it.
void __fastcall sub_4BBDD0(CUnit **ppSlot, int nCount,
                           CUnit *pUnit, CUnit *pTarget);

// 0x4BCE70. Commits a resolved selection, unless a
// single-unit selection is suppressed by the key-modifier state.
void __fastcall sub_4BCE70(int nCount, CUnit **ppUnits, int a3, int a4);

// ---------------------------------------------------------------------------
// the drag lifecycle
// ---------------------------------------------------------------------------
// The family below is declared here and not only in the TU, because a generated
// hook row's declaration has to come from a header on the include path.

// The three mouse handlers. Each takes the event in ECX and returns void; they
// are installed into gEventHooks[] by usel_drag_start.
void __fastcall usel_drag_start(dlgevt_t *pEvent);
void __fastcall usel_drag_moved(dlgevt_t *pEvent);
void __fastcall usel_drag_end(dlgevt_t *pEvent);

// 0x4BCEB0. Picks the stat_txt.tbl string id for a rejected left click. The
// signature comes from its only caller, usel_click_target: the second argument is
// passed as one byte with no widening, the third is the address of a local the
// caller then compares as a WORD, and `ret 4h` confirms one stack slot.
void __fastcall sub_4BCEB0(CUnit *pTarget, weapon_id_t bWeaponId, WORD *pwErrStr);

// The two halves of the drag's terminal dispatch.

// 0x4BBE70. The click half. The arity comes from EDX rather than ECX, which
// already holds a value from the width test before the call: the `mov edx, esi`
// has no other consumer, so EDX is a parameter, and __fastcall cannot pass a
// second register argument without the first.
void __fastcall usel_click_select(int nX, int nY);

// 0x4BC2D0. The band-select half. One register parameter: the address the caller
// puts in ECX is &sgSelectRect exactly.
void __fastcall usel_drag_select(rect16_t *prc);

// 0x4BC4C0. Merges a freshly gathered list into the existing selection, honouring
// the 12-unit cap: scan for the first free slot, then append or replace by
// priority through sub_4BBDD0. ppSel is the existing selection, ppNew the
// gathered candidates, and it returns the resulting count.
int __fastcall sub_4BC4C0(CUnit **ppSel, CUnit **ppNew, int nNew, CUnit *pTarget);

// 0x4BC5B0. The band gather: walk the candidate list and fill the selection
// array, promoting subunits, de-duplicating, and applying the cap through
// sub_4BBDD0. Unported, but declared because usel_drag_select calls it. ECX is a
// cursor into the candidate list, EDX the output array, and the one stack slot is
// the unit that must stay selected.
int __fastcall sub_4BC5B0(CUnit **ppIter, CUnit **ppSel, CUnit *pTarget);

// ---------------------------------------------------------------------------
// point hit-testing
// ---------------------------------------------------------------------------

// 0x4BC400. Is this unit clicked at (nX, nY)? The sprite's bounding box, then the
// pixel-accurate test, then the subunit -- a tank's turret answers for its tank.
// A free function taking CUnit * in ECX, where its own callee at 0x4BC460 is a
// class member; both spellings are the ledger's. It returns the hit value and not
// a normalised bool: every exit is whatever the last call left in EAX.
int __fastcall sub_4BC400(CUnit *pUnit, int nX, int nY);

// 0x4BCD00. The click target: of everything under the cursor, pick the unit the
// player meant -- visible, selectable and on top, with ties broken by the
// smallest placement box.
CUnit *__fastcall get_map_target(int nX, int nY);

// 0x4BC820. The declaration below is character-identical to src117/h/minimap.h's,
// `__fastcall` included: that header already declares this function, so a second
// spelling is a C2371 there, and two spellings under one include root are
// ambiguous to the generator, which then emits no thunk at all. Its derivation,
// kept: the coordinates are int rather than WORD, the zero-extension in front of
// the call proving it, where a 16-bit parameter would need a plain move.
void __fastcall usel_click_target(int nX, int nY, intptr_t a3, int a4);


// console_collision (0x43E890) has moved to src117/h/cursor.h, now that a
// cursor.cpp exists and defines it. This header includes that one at the top, so
// every existing consumer keeps compiling unchanged; its explicit `__fastcall`
// was dropped there, /Gr making it the default and the mangling identical.

#endif // OC117_USEL_H
