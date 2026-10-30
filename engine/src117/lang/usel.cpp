// usel.cpp -- unit selection: the drag-rectangle lifecycle plus the click and
// band target resolution that turns a screen point or rectangle into the player's
// 12-unit selection. Oracle 0x4BBDD0-0x4BCF27.
//
// Adding or removing a callee in this file breaks a body here in either
// direction. usel_cancel_drag_evt needs usel_cancel_drag inlined -- the oracle's
// evt body is cancel_drag's bytes with the `ret` replaced by a tail jump, and the
// out-of-line copy still exists because the function's address is taken. And
// usel_type_unselectable must not be inlined: sub_4BC5B0 emits a real call to it,
// which survives because the compiler will not inline a jump-table switch.
#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include <string.h>        // memset/memcpy -- usel_drag_select's zeroing is a real
                           // `call memset` in the oracle while its 48-byte copy
                           // is inlined, so the two are not interchangeable.
#include "platform.h"
#include "point16.h"
#include "rect16.h"
#include "rect32.h"        // gGameMapRect -- 16 B, the mouse cage
#include "point32.h"      // CUnit::sub_4BC460 takes one by value
#include "dialog.h"
#include "CUnit.h"
#include "CSprite.h"
#include "CImage.h"        // CImage::ClickableAt -- needs the COMPLETE type
#include "CUnitFind.h"     // get_units_at_cursor_point / finish_unit_iteration
                           // -- ours; see that header on the gen reach it severs
#include "bmp.h"           // bmp_rect      -- unsigned short params, proven
#include "dlg_dlgdraw.h"   // img_area_invalid -- int params, proven
#include "usel.h"
#include "group_un.h"       // sub_464030
#include "placebox.h"      // ours: set_placebox
#include "target.h"       // ours: target_off
#include "statbtn.h"       // target_is_on
#include "CUnitInit.h"     // get_order_tech_energy_cost
#include "CUnitHarvest.h"  // gamesnd_show_player_error
#include "order.h"         // gamesnd_error, status_get_str
#include "gamesnd.h"       // gamesnd_ack
#include "CUnitCombat.h"   // IsVulnerableType
#include "net_cmds.h"      // send_net_cmd_Action
#include "../port/render/port_view.h"  // Hyperion: PortViewScreenToMap, g_portView
#include "../h/playervision.h"  // Hyperion: the replay viewer past slot 7 (stage D)
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// ---------------------------------------------------------------------------
// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload, with one guard per name.

// gbSelectDragging (0x6CA720) is non-zero while a band box is live. A BYTE:
// every access is byte-wide.
#ifdef OC_DLL_MODE
  #ifndef gbSelectDragging
  #define gbSelectDragging (*reinterpret_cast<BYTE *>(OC_VA(0x6CA720u)))
  #endif
#else
extern BYTE gbSelectDragging;
#endif

// sgSelectStart (0x6CA724) is the anchor the band box is dragged out from, and it
// is one object rather than two shorts: usel_drag_moved reads it both as a full
// 32-bit load using only the low half and as a 16-bit load of the second member,
// and the compiler coalesces two adjacent 16-bit loads that way only within one
// object. The next named global sits 4 bytes on, which is its extent.
#ifdef OC_DLL_MODE
  #ifndef sgSelectStart
  #define sgSelectStart (*reinterpret_cast<point16_t *>(OC_VA(0x6CA724u)))
  #endif
#else
extern point16_t sgSelectStart;
#endif

// sgSelectRect (0x6CA728) is the band box in screen coordinates. Signed shorts,
// and the consumers prove the sign: three bodies widen the members with `movsx`
// into img_area_invalid's int parameters, where a WORD member would give `movzx`.
// A rect clipped against the map edge is where a negative left or top arises.
// It is one object on the same argument as sgSelectStart -- usel_draw_rect mixes
// 32-bit and 16-bit loads of it in one body -- and the next name sits 8 bytes on,
// exactly sizeof(rect16_t).
#ifdef OC_DLL_MODE
  #ifndef sgSelectRect
  #define sgSelectRect (*reinterpret_cast<rect16_t *>(OC_VA(0x6CA728u)))
  #endif
#else
extern rect16_t sgSelectRect;
#endif

// gbIsDoubleClickEvent (0x6CA730) is a DWORD despite the name the sheet gives it:
// usel_drag_start stores it dword-wide.
#ifdef OC_DLL_MODE
  #ifndef gbIsDoubleClickEvent
  #define gbIsDoubleClickEvent (*reinterpret_cast<DWORD *>(OC_VA(0x6CA730u)))
  #endif
#else
extern DWORD gbIsDoubleClickEvent;
#endif

// byte_665E6A is a key-modifier latch owned by event_Win.cpp: WndProc writes it
// twice and get_key_modifiers reads it. It is left unnamed, since the sheet has no
// name for it and inventing one from four call sites would assert a meaning we
// have not proven; byte_665E68, which this TU's other bodies read, is treated the
// same way.
//
// Both are probably elements of gKeyTbl (0x665E58), whose extent runs 0x100 bytes
// to the next name -- a 256-entry virtual-key state table, in which 0x665E68 is
// VK_SHIFT and 0x665E6A is VK_MENU. Additive band-select being shift-click, and
// the other latch suppressing a single-unit group, is what those two keys would
// do here. They are left as absolute addresses rather than written as gKeyTbl
// elements because the accesses are absolute and unindexed, so they cannot show
// which spelling is right, and both compile the same.
#ifdef OC_DLL_MODE
  #ifndef byte_665E6A
  #define byte_665E6A (*reinterpret_cast<BYTE *>(OC_VA(0x665E6Au)))
  #endif
#else
extern BYTE byte_665E6A;
#endif

// gwMouseFlags (0x6D315C) is zero once the mouse capture that owns the band box
// has been released, which is what makes usel_draw_rect tear the box down instead
// of drawing it. A WORD, from its one compare.
#ifdef OC_DLL_MODE
  #ifndef gwMouseFlags
  #define gwMouseFlags (*reinterpret_cast<WORD *>(OC_VA(0x6D315Cu)))
  #endif
#else
extern WORD gwMouseFlags;
#endif

// _bmp_pen (0x6D0C50), a BYTE: the current pen index.
#ifdef OC_DLL_MODE
  #ifndef _bmp_pen
  #define _bmp_pen (*reinterpret_cast<BYTE *>(OC_VA(0x6D0C50u)))
  #endif
#else
extern BYTE _bmp_pen;
#endif

// sgbPens_g (0x6D2368) is BYTE[32]. The 0x6D237A usel_draw_rect reads is its
// element 18, not a global of its own.
#ifdef OC_DLL_MODE
  #ifndef sgbPens_g
  #define sgbPens_g (reinterpret_cast<BYTE *>(OC_VA(0x6D2368u)))
  #endif
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif

// ---------------------------------------------- the mouse-handler slot family
// The two slot families differ in arity, and typing them alike would be an ABI
// error the bytes could never show.

// The event-hook type comes from the dispatch site and not the callee: a callee
// that ignores its register argument looks the same as one that has none, so only
// the caller decides. SendMouseMovedEvt builds a 0x14-byte event, puts its
// address in ECX and sets nothing else, so one register argument, a dlgevt_t *.
// That frame size corroborates src117/h/dialog.h's own assert.
typedef void (__fastcall *PFN_EVT_HOOK)(dlgevt_t *pEvent);

// The cancel slot is nullary, a different type, from its only reader: init_events
// calls through it with no register set up at all. That is what makes usel.h's
// nullary usel_cancel_drag assignable here with no cast, and why gpfCancelDrag is
// not a PFN_EVT_HOOK.
typedef void (__fastcall *PFN_CANCEL_DRAG)(void);

// gpfCancelDrag (0x665DEC), owned by event_Win.cpp.
#ifdef OC_DLL_MODE
  #ifndef gpfCancelDrag
  #define gpfCancelDrag (*reinterpret_cast<PFN_CANCEL_DRAG *>(OC_VA(0x665DECu)))
  #endif
#else
extern PFN_CANCEL_DRAG gpfCancelDrag;
#endif

// gEventHooks (0x665DF0) is PFN_EVT_HOOK[19], and the three addresses
// usel_drag_start stores handlers into -- 0x665DFC, 0x665E04 and 0x665E10 -- are
// its elements 3, 5 and 8 rather than globals of their own. The extent comes from
// init_events memsetting 0x4C bytes over the whole object, and the next name
// begins exactly there.
#ifdef OC_DLL_MODE
  #ifndef gEventHooks
  #define gEventHooks (reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x665DF0u)))
  #endif
#else
extern PFN_EVT_HOOK gEventHooks[19];
#endif

// gGameMapRect (0x666430) is 16 bytes, the size src117/h/rect32.h asserts and the
// gap to the next name. Only its address is taken here, so nothing in this file
// says anything about the member order.
#ifdef OC_DLL_MODE
  #ifndef gGameMapRect
  #define gGameMapRect (*reinterpret_cast<rect32_t *>(OC_VA(0x666430u)))
  #endif
#else
extern rect32_t gGameMapRect;
#endif

// glScrollPixelX and glScrollPixelY (0x543218 and 0x54321C): usel_drag_end adds
// them to convert the band box from screen to map coordinates.
#ifdef OC_DLL_MODE
  #ifndef glScrollPixelX
  #define glScrollPixelX (*reinterpret_cast<LONG *>(OC_VA(0x543218u)))
  #endif
#else
extern LONG glScrollPixelX;
#endif
#ifdef OC_DLL_MODE
  #ifndef glScrollPixelY
  #define glScrollPixelY (*reinterpret_cast<LONG *>(OC_VA(0x54321Cu)))
  #endif
#else
extern LONG glScrollPixelY;
#endif

// gwMapPixHeight (0x543234) also comes from the generated header, which the guard
// here allows for. usel_drag_end reads it at both widths, so the WORD is not a
// guess.
#ifdef OC_DLL_MODE
  #ifndef gwMapPixHeight
  #define gwMapPixHeight (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
  #endif
#else
extern upos_t gwMapPixHeight;
#endif

// wpn_targetErrStr_g (0x5261C0) is a weapons.dat-indexed stat_txt.tbl string id,
// stride 2 and so WORD. The stride is what matters here; the declared extent of
// 130 is inferred, from the weapons.dat row count recorded elsewhere and from the
// gap to the next name, which is that count plus alignment. sub_4BCEB0 compares
// its second parameter against that same 130 as a sentinel rather than a bound,
// which is what makes it a weapon id.
#ifdef OC_DLL_MODE
  #ifndef wpn_targetErrStr_g
  #define wpn_targetErrStr_g (reinterpret_cast<WORD *>(OC_VA(0x5261C0u)))
  #endif
#else
extern WORD wpn_targetErrStr_g[130] OC_UNALIGNED;
#endif

// ----------------------------------------- globals owned by other TUs

// gulUnitTypeFlags (0x521660) is the units.dat flags table, indexed by the
// unbiased wType. It is read at two widths here, the byte one being the compiler
// narrowing a low-byte mask on a dword element rather than a BYTE array;
// declaring it BYTE[] would break the 0x100 test.
#ifdef OC_DLL_MODE
  #ifndef gulUnitTypeFlags
  #define gulUnitTypeFlags (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
  #endif
#else
extern DWORD gulUnitTypeFlags[228] OC_UNALIGNED;
#endif

// gPlayback (0x67CC90) is non-zero in a replay, so every visibility test here is
// gated on it: an observer sees units it does not own.
#ifdef OC_DLL_MODE
  #ifndef gPlayback
  #define gPlayback (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
  #endif
#else
extern DWORD gPlayback;
#endif

// gdwLocalMask (0x672668) is the local player's vision bit. get_map_target loads
// it once and uses it at both widths, so the narrowing has to be a cast at the
// point of use rather than a narrower declared type.
#ifdef OC_DLL_MODE
  #ifndef gdwLocalMask
  #define gdwLocalMask (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
  #endif
#else
extern DWORD gdwLocalMask;
#endif

// g_LocalNationID (0x508D50) is distinct from g_ActiveNationID (0x508D44), which
// usel_click_select also reads: conflating the two would be a behavioural bug no
// byte comparison could show.
#ifdef OC_DLL_MODE
  #ifndef g_LocalNationID
  #define g_LocalNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D50u)))
  #endif
#else
extern DWORD g_LocalNationID;
#endif

// gpNewUnits (0x6A6070) is the staging array a band select builds before it is
// committed, CUnit *[12]. The extent comes two ways that agree: the gap to the
// next name is 48 bytes, and usel_drag_select copies exactly 48 bytes out of it in
// three chunks. That 12 is the same selection cap USEL_MAX_SELECT derives
// elsewhere, and the two addresses inside the copy are those chunk boundaries
// rather than globals of their own.
#ifdef OC_DLL_MODE
  #ifndef gpNewUnits
  #define gpNewUnits (reinterpret_cast<CUnit **>(OC_VA(0x6A6070u)))
  #endif
#else
extern CUnit *gpNewUnits[SELECTION_MAX] OC_UNALIGNED;
#endif

// g_ActiveNationID (0x508D44), owned by net_glue.cpp.
#ifdef OC_DLL_MODE
  #ifndef g_ActiveNationID
  #define g_ActiveNationID (*reinterpret_cast<DWORD *>(OC_VA(0x508D44u)))
  #endif
#else
extern DWORD g_ActiveNationID;
#endif

// byte_665E68 is usel_drag_select's additive-select latch, read once into a
// register at entry. Unnamed on the same grounds as byte_665E6A above, and very
// likely gKeyTbl[0x10], VK_SHIFT.
#ifdef OC_DLL_MODE
  #ifndef byte_665E68
  #define byte_665E68 (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
  #endif
#else
extern BYTE byte_665E68;
#endif

// byte_665E69 is the third latch of the same trio, and the one that says what the
// trio is: the three sit at gKeyTbl[0x10], [0x11] and [0x12], VK_SHIFT,
// VK_CONTROL and VK_MENU, which fits what each does here -- Shift adds to the
// live selection, Ctrl switches to the select-all-of-type-on-screen path that
// bands the whole viewport rect, and Alt suppresses a single-unit commit.
#ifdef OC_DLL_MODE
  #ifndef byte_665E69
  #define byte_665E69 (*reinterpret_cast<BYTE *>(OC_VA(0x665E69u)))
  #endif
#else
extern BYTE byte_665E69;
#endif

// --------------------------------------------- the visibility and hit-test set
// The two vision masks are each loaded once and used at two widths in one body,
// which is what fixes their type: a DWORD narrowed by a cast at the byte use is
// the only spelling that produces both.

// gdwReplayVisionMask (0x67CC84) is a DWORD, from the pair of reads in
// get_map_target: a dword one for the test against the unit's vision and a byte
// one for the test against the sprite's.
#ifdef OC_DLL_MODE
  #ifndef gdwReplayVisionMask
  #define gdwReplayVisionMask (*reinterpret_cast<DWORD *>(OC_VA(0x67CC84u)))
  #endif
#else
extern DWORD gdwReplayVisionMask;
#endif

// sgbShowAll_g (0x67CC88) is a DWORD despite the name, both readers comparing it
// dword-wide; the same disagreement gbIsDoubleClickEvent has.
#ifdef OC_DLL_MODE
  #ifndef sgbShowAll_g
  #define sgbShowAll_g (*reinterpret_cast<DWORD *>(OC_VA(0x67CC88u)))
  #endif
#else
extern DWORD sgbShowAll_g;
#endif

// gUnitPlaceboxSize (0x522728) is point16_t[228]. The layout is read off one
// body: stride 4, two 16-bit members, and `movsx` makes them signed. The extent
// is the units.dat row count, indexed by the same unbiased wType that indexes
// gulUnitTypeFlags two lines away in the same loop.
#ifdef OC_DLL_MODE
  #ifndef gUnitPlaceboxSize
  #define gUnitPlaceboxSize (reinterpret_cast<point16_t *>(OC_VA(0x522728u)))
  #endif
#else
extern point16_t gUnitPlaceboxSize[228] OC_UNALIGNED;
#endif

// ---------------------------------------------------------------------------
// callees owned by other TUs
// ---------------------------------------------------------------------------
// Declared here rather than in usel.h on purpose: keeping a callee owned by
// another TU out of the shared header avoids a second, disagreeing declaration
// where a generated TU also sees the owning TU's own header.

// game_reset_handlers (0x448DD0) restores the default mouse handler set. It is
// reached as a tail jump from usel_cancel_drag_evt and from usel_draw_rect's
// teardown arm, so its result is never consumed. Spelled as
// src117/h/event_Win.h spells it.
void game_reset_handlers();

// select_units (0x49CF80) commits a resolved unit list as the local player's
// selection. sub_4BCE70 forwards all four of its own arguments unchanged, in the
// same registers and the same stack order.
void __fastcall select_units(int nCount, CUnit **ppUnits, BYTE a3, int a4);

// ------------------------------------------------------------- the drag arm
// console_collision (0x43E890) is declared in src117/h/cursor.h, since
// placebox.cpp is a second caller and a callee declared only in a .cpp is not a
// seat our generated code can include.

// target_is_on, target_off and placebox_is_on (0x4B28E0, 0x4B2900, 0x47D2E0) are
// spelled character-identically to src117/h/statbtn.h's and not re-derived: a
// second, disagreeing spelling of an already-declared symbol is a C2371 in a TU
// this file never touches, and two spellings under one include root are ambiguous
// to the generator, which then emits no thunk.
BYTE __fastcall target_is_on();
int  __fastcall placebox_is_on();

// set_placebox (0x47D300) is declared in src117/h/placebox.h; see the note at the
// same site in src117/lang/CUnitGUI.cpp. Its first and third parameters are not
// fully derived -- the one call site here passes constants that compile the same
// at every candidate width -- so int is the conservative spelling.

// mou_cager (0x4CE850) is spelled character-identically to src117/h/minimap.h's,
// which minimap.cpp already calls, although two call sites show that declaration
// is under-derived: usel_drag_start passes the address of gGameMapRect and
// init_events passes zero, so the parameter is a rect pointer and minimap.cpp's
// `mou_cager(0)` is a null uncage rather than mode 0. Not corrected here, for the
// reason above; retyping it tree-wide is its own change (docs/open-issues.md).
void __fastcall mou_cager(intptr_t nMode);

// cursor_set and DetermineCursor (0x43E9A0 and 0x43E6D0) are declared in
// src117/h/cursor.h, which usel.h includes, with their derivations; they were
// declared here until a cursor.cpp existed.

// find_units_in_rect (0x4224C0, in CUnitFind.cpp) is not get_units_in_rect, which
// src117/lang/order.cpp declares at 0x422330: two different functions, both
// pushing a finder frame and both returning a NULL-terminated list, so the names
// read as synonyms and are not. The rect arrives in ECX, read as four 16-bit
// members whose `movsx` re-confirms rect16.h's signedness, and the return is
// CUnit ** because sub_4BC5B0 dereferences it.
CUnit **__fastcall find_units_in_rect(rect16_t *pRect);

// get_units_in_rect (0x422330) is the sibling of the function above and not the
// same one. Declared character-identically to src117/lang/order.cpp's, which
// already calls it, since a second differing spelling would be ambiguous to the
// generator, silently. usel_click_select uses it for the Ctrl
// select-all-of-type path, banding the whole viewport rect.
CUnit **__fastcall get_units_in_rect(rect16_t *pRect);

// unit_is_cloaked_by_arbiter (0x4A04E0), in statbtn.cpp.
int __fastcall unit_is_cloaked_by_arbiter(CUnit *pUnit);

// IsSelfOwned (0x40FD30) is `!gPlayback && pUnit->bPlayer == g_LocalNationID`,
// returning a literal 1 or 0, with one ECX argument and no stack arguments. It is
// declared in CImage.h, its owner TU's header.

// The ownership test is a macro and not a helper function on purpose: spelled as
// a function, the argument becomes one value the compiler spills across the
// intervening call and reloads, which costs 8 bytes in sub_4BC4C0 and takes a
// frame slot that body's own local wants. As a macro each use re-reads the
// operand, which is what keeps the player byte live in CL for the compare that
// follows.
#define USEL_IS_LOCAL_MULTISELECT(u) \
  ((u)->IsMultiSelectable() && !gPlayback && (u)->bPlayer == g_LocalNationID)

// The oracle uses both spellings of this predicate, so do not unify them: the
// macro above is the inlined form sub_4BC4C0 and sub_4BC5B0 need, while
// usel_click_select emits a real call to IsSelfOwned for the same test.
// Collapsing them either way breaks one of the three bodies.

// get_units_at_cursor_point and finish_unit_iteration are declared in
// src117/h/CUnitFind.h and not here: they have to live in a header we own,
// because our generated code includes the owning TU's header for an unported
// callee, and without ours it reached the quarantined tree's spelling.

// ---------------------------------------------------------------------------
// the bodies
// ---------------------------------------------------------------------------

// A global flag guard, then the rect invalidate. Unverified.
void __fastcall usel_cancel_drag(void)
{
  if (gbSelectDragging) {
    img_area_invalid(sgSelectRect.left, sgSelectRect.top,
                     sgSelectRect.right, sgSelectRect.bottom);
    gbSelectDragging = 0;
  }
}

// The same, plus a tail call, and it relies on usel_cancel_drag being inlined
// (see the head of this file). Unverified.
void __fastcall usel_cancel_drag_evt(void)
{
  if (gbSelectDragging) {
    usel_cancel_drag();
    game_reset_handlers();
  }
}

// A thin guarded forwarder. Unverified.
void __fastcall sub_4BCE70(int nCount, CUnit **ppUnits, int a3, int a4)
{
  if (nCount == 1 && byte_665E6A) {
    if (sub_464030(ppUnits[0]->GetUnitID()))
      return;
  }
  select_units(nCount, ppUnits, a3, a4);
}

// An array-cursor scan with a shared-tail store. The comparison is unsigned,
// because GetSpritePriority returns a DWORD. Unverified.
void __fastcall sub_4BBDD0(CUnit **ppSlot, int nCount,
                           CUnit *pUnit, CUnit *pTarget)
{
  sprite_key_t dwPrio = pUnit->pSprite->GetSpritePriority();  // Hyperion: DWORD in 1.17 (stage D)

  while (nCount) {
    CUnit *pCur = *ppSlot;
    --nCount;
    if (pCur != pTarget) {
      if (dwPrio > pCur->pSprite->GetSpritePriority() || pUnit == pTarget) {
        *ppSlot = pUnit;
        return;
      }
    }
    ++ppSlot;
  }
}

// The drag lifecycle's declarations live in src117/h/usel.h and not here, since a
// generated hook row needs a header declaration and one spelling in one place is
// what keeps the name unambiguous. usel_drag_moved must not be inlined into
// usel_drag_end, where the oracle emits a real call; at 166 bytes with two calls
// of its own it is far past the inline budget, so nothing here asks for that.

// Two arms, draw and teardown, over the same rect.
void __fastcall usel_draw_rect(void)
{
  if (gbSelectDragging) {
    if (gwMouseFlags) {
      _bmp_pen = sgbPens_g[18];
      bmp_rect(sgSelectRect.left, sgSelectRect.top,
               sgSelectRect.right - sgSelectRect.left + 1,
               sgSelectRect.bottom - sgSelectRect.top + 1);
    } else {
      img_area_invalid(sgSelectRect.left, sgSelectRect.top,
                       sgSelectRect.right, sgSelectRect.bottom);
      gbSelectDragging = 0;
      game_reset_handlers();
    }
  }
}

// ---------------------------------------------------------------------------
// the drag lifecycle
// ---------------------------------------------------------------------------

// 0x4BC140. The mouse came up: finish the drag, convert the band box from screen
// to map coordinates, clamp it to the map, and dispatch either a click -- a box 4
// pixels or smaller in both axes -- or a band select. It takes the event and
// forwards it untouched, never writing ECX before the call.
//
// The four edges are updated in place rather than through short locals, which
// compile to 32-bit loads and adds truncated at the end; the oracle's narrow adds
// are what a 16-bit member `+=` gives. Hoisting the two scroll deltas into locals
// then makes the compiler lead with the scroll load, as the oracle does, and
// `gbSelectDragging = 0` comes first in source although the oracle emits it after
// the four loads, since it is the earliest store.
//
// The first branch goes to the shared epilogue and not to the band-select arm,
// one byte of displacement with real behaviour behind it: a band box whose top is
// already past the bottom of the map does nothing at all.
void __fastcall usel_drag_end(dlgevt_t *pEvent)
{
  usel_drag_moved(pEvent);
  game_reset_handlers();
  cursor_set(DetermineCursor());

  gbSelectDragging = 0;
  // Hyperion: both corners through the camera (port_view.cpp)
  {
    int nLeft, nTop, nRight, nBottom;
    PortViewScreenToMap(sgSelectRect.left, sgSelectRect.top, &nLeft, &nTop);
    PortViewScreenToMap(sgSelectRect.right, sgSelectRect.bottom, &nRight, &nBottom);
    sgSelectRect.left = static_cast<short>(nLeft);
    sgSelectRect.top = static_cast<short>(nTop);
    sgSelectRect.right = static_cast<short>(nRight);
    sgSelectRect.bottom = static_cast<short>(nBottom);
  }

  int nMapH = gwMapPixHeight;
  if (sgSelectRect.top < nMapH) {
    if (sgSelectRect.bottom >= nMapH)
      sgSelectRect.bottom = PosS(gwMapPixHeight - 1);
    if (sgSelectRect.right - sgSelectRect.left <= 4 &&
        sgSelectRect.bottom - sgSelectRect.top <= 4)
      usel_click_select(sgSelectRect.left, sgSelectRect.top);
    else
      usel_drag_select(&sgSelectRect);
  }
}

// 0x4BC220. The mouse moved with the button down: invalidate the old band box, or
// start dragging, then rebuild it as the normalised rectangle between the anchor
// and the cursor and invalidate that. The normalisation is a min and a max with a
// tail-merged `right` store -- each arm stores `left` and leaves the other
// endpoint in AX, and the two join on one store, which two explicit arms give
// where a min/max pair would not.
void __fastcall usel_drag_moved(dlgevt_t *pEvent)
{
  if (gbSelectDragging) {
    img_area_invalid(sgSelectRect.left, sgSelectRect.top,
                     sgSelectRect.right, sgSelectRect.bottom);
  } else {
    gbSelectDragging = 1;
  }

  if (sgSelectStart.x <= pEvent->nX) {
    sgSelectRect.left  = sgSelectStart.x;
    sgSelectRect.right = pEvent->nX;
  } else {
    sgSelectRect.left  = pEvent->nX;
    sgSelectRect.right = sgSelectStart.x;
  }

  if (sgSelectStart.y <= pEvent->nY) {
    sgSelectRect.top    = sgSelectStart.y;
    sgSelectRect.bottom = pEvent->nY;
  } else {
    sgSelectRect.top    = pEvent->nY;
    sgSelectRect.bottom = sgSelectStart.y;
  }

  img_area_invalid(sgSelectRect.left, sgSelectRect.top,
                   sgSelectRect.right, sgSelectRect.bottom);
}

// 0x4BCB80. The mouse went down on the game screen: dismiss any targeting or
// placement cursor, cage the mouse to the map, install the drag handlers, record
// the anchor, and latch whether this was a double click. The four handler stores
// are gpfCancelDrag and three gEventHooks slots in the oracle's own order, which
// is not address order, so it is copied rather than tidied.
//
// The double-click tail is a switch, and its "store 0 then store 1" is the
// compiler's doing rather than the source's: written as a guard plus an
// assignment it emits a cmove through registers and reloads the tag instead of
// decrementing the residue. What matches is a plain three-arm switch, whose arms
// differ only in the stored constant, so the default arm's store is emitted
// unconditionally and the last case overwrites it; neither store is sequenced
// before the other in the source. The subtract chain is the tell for a switch,
// appearing only when the tested value is dead afterwards, and the two empty
// cases are what make their jumps land on the shared `ret`.
void __fastcall usel_drag_start(dlgevt_t *pEvent)
{
  if (console_collision(pEvent->nX, pEvent->nY))
    return;

  if (target_is_on())
    target_off();
  if (placebox_is_on())
    set_placebox(0, NULL, 1);

  mou_cager(reinterpret_cast<intptr_t>(&gGameMapRect));

  gpfCancelDrag  = usel_cancel_drag;
  gEventHooks[5] = usel_drag_end;
  gEventHooks[8] = usel_drag_end;
  gEventHooks[3] = usel_drag_moved;
  cursor_set(9);

  sgSelectStart.x = pEvent->nX;
  sgSelectStart.y = pEvent->nY;

  switch (pEvent->wTag) {
    case 3:
    case 5:
      break;
    case 6:
      gbIsDoubleClickEvent = 1;
      break;
    default:
      gbIsDoubleClickEvent = 0;
      break;
  }
}

// 0x4BCEB0. Chooses the stat_txt.tbl string id for a rejected left click: a
// stasis or disabled-style message when the target itself forbids the order,
// otherwise the attacking weapon's own target-error message.
//
// The signature comes from its only caller, usel_click_target: the second
// argument arrives as one byte with no widening, the third is the address of a
// local the caller then compares as a WORD, and `ret 4h` confirms one stack slot.
// ECX is a CUnit *, null-tested and then read at dwFlags and bStasisTimer.
//
// Every 16-bit constant goes through a register, because the compiler avoids the
// prefixed imm16 forms; that is its choice and not a source construct. The
// 0x4000000 bit is left unnamed: src117/h/CUnit.h records it as an unnamed bit
// tested the same way by CUnit::CanTargetWithWeapon, and inventing a name from
// two call sites would assert a meaning we have not proven.
void __fastcall sub_4BCEB0(CUnit *pTarget, weapon_id_t bWeaponId, WORD *pwErrStr)
{
  if (pTarget != NULL && (pTarget->dwFlags & 0x4000000) != 0) {
    if (pTarget->bStasisTimer)
      *pwErrStr = 0x384;
    else if ((pTarget->dwFlags & CUnit::UF_Building) != 0)
      *pwErrStr = 0x36D;
    else
      *pwErrStr = 0x36C;
    return;
  }

  if (*pwErrStr == 0xFFFF)
    *pwErrStr = 0x36F;
  if (bWeaponId != WEAPON_NONE)
    *pwErrStr = wpn_targetErrStr_g[bWeaponId];
}

// ---------------------------------------------------------------------------
// point hit-testing
// ---------------------------------------------------------------------------
// CUnit::sub_4BC460 sits below sub_4BC400, which still calls it rather than
// inlining it. usel_type_unselectable (0x4BC730) is absent from this file and its
// call is thunked to the oracle.

// Is this unit clicked at (nX, nY)? The
//            sprite's bounding box first, then the pixel-accurate test, then the
//            subunit -- a tank turret answers for its tank.
//
// It returns the hit value, not a normalised bool, so IsUnderPoint's result is
// kept in nHit. A miss falls into the subunit check rather than returning, so a
// unit whose own sprite is not under the point still answers through its subunit.
int __fastcall sub_4BC400(CUnit *pUnit, int nX, int nY) {
  int nHit = pUnit->pSprite->IsUnderPoint(nX, nY);
  if (nHit) {
    // pSprite is loaded once into ECX ahead of the branch and the second arm
    // does `mov ecx, dword ptr [ecx+18h]` off it, so both arms must be spelled
    // off the same `pUnit->pSprite` for MSVC to CSE the load.
    if (gulUnitTypeFlags[pUnit->wType] & 0x100)
      nHit = pUnit->pSprite->IsClickablePt(nX, nY);
    else
      nHit = pUnit->pSprite->pMainImage->ClickableAt(nX, nY);
  }

  if (nHit)
    return nHit;
  if (pUnit->subunit) {
    point32_t pt = {nX, nY};
    return pUnit->subunit->sub_4BC460(pt);
  }
  return nHit;
}

// Point hit test without the subunit recursion: the sprite's bounding box, then
// the pixel test. The point comes by value; two int parameters home x and y
// in each other's callee-saved register.
int CUnit::sub_4BC460(point32_t pt) {
  int nX = pt.x, nY = pt.y;
  if (!this->pSprite->IsUnderPoint(nX, nY))
    return 0;
  if (gulUnitTypeFlags[this->wType] & 0x100)
    return this->pSprite->IsClickablePt(nX, nY);
  return this->pSprite->pMainImage->ClickableAt(nX, nY);
}


// ---------------------------------------------------------------------------
// held bodies: compiled for proof, guarded out of the payload
// ---------------------------------------------------------------------------
// Both bodies below are byte-exact and neither ships, for one tooling reason.
// They stay in the file, inside the payload guard, so the comparison can still
// re-prove them while the payload build leaves them out and calls the oracle's
// copies.
//
// The two are coupled. usel_type_unselectable byte-matches, its jump table and
// all 195 index bytes included, but the comparison reports a size mismatch,
// because the oracle's own records give this jump-table function's extent as code
// only where the compiler puts its tables in the same COMDAT. And
// get_map_target's bytes need that definition present: without it the compiler
// emits a narrow move where the oracle zero-extends. Declaring the callee `int`
// would force the wider form and is refused, the callee's own body proving the
// parameter is 16-bit.

// The click target: of everything under the
//            cursor, pick the unit the player meant -- visible, selectable,
//            and on top; ties broken by the smallest placement box.
//
// The two masks are each loaded once and used at two widths, a BYTE test against
// the sprite's visibility field and a DWORD test against CUnit+0xE4, so the
// narrowing is a cast at the point of use rather than a narrow global.
CUnit *__fastcall get_map_target(int nX, int nY) {
  CUnit *pBest = NULL;
  int nBestArea = 0x7FFFFFFF;

  CUnit **ppList = get_units_at_cursor_point(nX, nY);
  CUnit *pUnit;
  for (CUnit **ppIter = ppList; (pUnit = *ppIter) != NULL; ++ppIter) {
    // units.dat flag 0x10 -- answer with the subunit (a turret's tank).
    if (gulUnitTypeFlags[pUnit->wType] & 0x10)
      pUnit = pUnit->subunit;

    if (gPlayback) {
      // Hyperion: or the viewer's slots past 7 (stage D)
      if (!(pUnit->pSprite->bVisibilityFlags & (BYTE)gdwReplayVisionMask) && !sgbShowAll_g &&
          !ViewHxSpriteSeen(pUnit->pSprite))
        continue;
      if ((pUnit->dwFlags & 0x300) && !(pUnit->dwUnionE4 & gdwReplayVisionMask) && !sgbShowAll_g &&
          !ViewHxDetected(pUnit))
        continue;
    } else {
      if (!(pUnit->pSprite->bVisibilityFlags & (BYTE)gdwLocalMask))
        continue;
      if ((pUnit->dwFlags & 0x300) && !(pUnit->dwUnionE4 & gdwLocalMask))
        continue;
    }

    if (usel_type_unselectable(pUnit->wType))
      continue;

    if (pBest == NULL) {
      nBestArea = gUnitPlaceboxSize[pUnit->wType].y * gUnitPlaceboxSize[pUnit->wType].x;
      pBest = pUnit;
      continue;
    }

    // The comparison is spelled `>=` and the arms are in this order because the
    // oracle's jump-taken arm is the higher-priority-candidate one, so the pBest
    // arm has to be the fall-through; inverted, the compiler swaps both blocks.
    // Unsigned, matching GetSpritePriority's DWORD return.
    // Hyperion: DWORD in 1.17; a Hyperion game's key is wider (stage D)
    sprite_key_t dwBestPriority = pBest->pSprite->GetSpritePriority();
    sprite_key_t dwPriority = pUnit->pSprite->GetSpritePriority();
    if (dwBestPriority >= dwPriority) {
      if (sub_4BC400(pBest, nX, nY))
        continue;
    } else {
      if (sub_4BC400(pUnit, nX, nY)) {
        nBestArea = SpriteGrpHeight(pUnit->pSprite) * SpriteGrpWidth(pUnit->pSprite);  // Hyperion: past 255
        pBest = pUnit;
        continue;
      }
    }

    int nArea = gUnitPlaceboxSize[pUnit->wType].y * gUnitPlaceboxSize[pUnit->wType].x;
    if (nArea < nBestArea) {
      nBestArea = nArea;
      pBest = pUnit;
    }
  }

  finish_unit_iteration(ppList);
  return pBest;
}

// ---------------------------------------------------------------------------
// 0x4BC730. The definition stays in this file although it does not ship, and that
// is not a contradiction. Its bytes are proven: the whole 239-byte COMDAT -- 36
// bytes of code, an 8-byte jump table and a 195-byte byte index table -- is
// identical to the oracle's, the jump table being {return 1, return 0} and the
// index bytes zero at exactly the eight positions for types 14, 85, 105, 202,
// 205, 206, 207 and 208. What holds it back is that the comparison records this
// function's extent as code only, where the compiler puts the tables in the same
// COMDAT. And get_map_target needs the definition present: without it the
// compiler emits a narrow move where the oracle zero-extends.
//
// The default returns 0, not 1: the out-of-range jump lands on the `xor eax, eax`.
// ---------------------------------------------------------------------------

int __fastcall usel_type_unselectable(WORD wType) {
  switch (wType) {
    case 0x0E:   // Nuclear Missile
    case 0x55:   // Scarab
    case 0x69:   // Disruption Web
    case 0xCA:   // Dark Swarm
    case 0xCD:   // Left Upper Level Door
    case 0xCE:   // Right Upper Level Door
    case 0xCF:   // Left Pit Door
    case 0xD0:   // Right Pit Door
      return 1;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// the selection gather and the cap loops
// ---------------------------------------------------------------------------
// Two opposite dependencies on this file's contents hold at once here, both read
// off the COMDAT table. sub_4BBDD0 must stay out of line, since both callers emit
// a real call to it. And sub_4BCE70 must be inlined, twice: the oracle splices its
// 62 bytes in at two sites, and that double inline is what produces the shared
// count fold where the early-return arm falls straight into the rally check. It
// still emits its own COMDAT, having external linkage, so it is both inlined here
// and reachable as a call.
//


// 0x4BC4C0. Merge a freshly-found list into a selection that already holds units:
// count what is there, bail out unless both the first candidate and the first
// incumbent are locally multi-selectable, then append every candidate that is not
// already in the original part of the array, handing overflow to sub_4BBDD0's
// replacement policy. The fourth parameter is forwarded unchanged as its pTarget.
int __fastcall sub_4BC4C0(CUnit **ppSel, CUnit **ppNew, int nNew, CUnit *pTarget)
{
  // A walking pointer rather than an indexed load, and the rotated pre-test is
  // what lets the empty-array edge jump past both tests below with nOld known 0.
  int nOld = 0;
  CUnit **pp = ppSel;
  while (*pp) {
    ++pp;
    ++nOld;
  }

  // Signed, and the return is the literal cap rather than the counter.
  if (nOld >= USEL_MAX_SELECT)
    return USEL_MAX_SELECT;

  if (nOld) {
    if (!USEL_IS_LOCAL_MULTISELECT(*ppNew))
      return nOld;
    if (!USEL_IS_LOCAL_MULTISELECT(*ppSel))
      return nOld;
  }

  int nSel = nOld;                        // [ebp-8] -- read after the loop

  // Written pre-rotated on purpose: the oracle guards with a signed test and
  // closes on an in-memory decrement and an equality exit, which is only valid
  // because the guard proved the count positive. A plain `while (nNew > 0)` keeps
  // the signed test at the bottom too, 6 bytes longer.
  if (nNew > 0) {
    // nAdded is scoped to the loop, and the scope matters: it is dead outside, so
    // the oracle zeroes it after the guard where nSel is zeroed before. At
    // function scope the compiler zeroes it first and then spends that register
    // as the guard's zero operand instead of the oracle's literal, which
    // reshuffles the whole preheader and its alignment pad with it.
    int nAdded = 0;                       // [ebp-4]
    do {
    // `*ppNew` is re-read at every use rather than cached in a local: a local
    // spills ppNew to the frame and costs the loop tail. The search covers the
    // original nOld entries only, downwards, and the oracle branches straight out
    // of it into the loop tail, so it is a goto rather than a found flag.
    int i = nOld;
    while (i) {
      --i;
      if (ppSel[i] == *ppNew)
        goto next;
    }

    // The arms are in this order because the branch sense is source-visible: the
    // oracle puts the sub_4BBDD0 call on the fall-through edge and exiles the
    // store block past the epilogue, which is the `if` body against the `else`.
    if (nSel >= USEL_MAX_SELECT) {
      // A cursor at the first appended slot, with nAdded and not nSel as the
      // count, so the policy can only displace units this call appended.
      sub_4BBDD0(&ppSel[nOld], nAdded, *ppNew, pTarget);
    } else {
      ppSel[nSel] = *ppNew;
      ++nSel;
      ++nAdded;
    }

  next:
      ++ppNew;
    } while (--nNew);
  }
  return nSel;
}

// Walk a finder iterator into a selection array: substitute subunits, drop
// duplicates and anything the local player cannot see or select, and keep only
// units matching pTarget when one is given. The last rejected but visible unit
// is the fallback. The tail sets nCount and returns it once; two `return`s put
// the commit block inline instead of past the epilogue.
int __fastcall sub_4BC5B0(CUnit **ppIter, CUnit **ppSel, CUnit *pTarget)
{
  int nCount = 0;
  CUnit *pFallback = NULL;

  if (pTarget) {
    ppSel[0] = pTarget;
    nCount = 1;
  }

  for (CUnit *pUnit = *ppIter; pUnit; pUnit = *++ppIter) {
    if (pUnit == pTarget)
      continue;

    // The duplicate search sits inside the subunit arm, as in the oracle.
    CUnit *pSub = pUnit->subunit;
    if (pSub) {
      if (gulUnitTypeFlags[pUnit->wType] & 0x10)
        pUnit = pSub;
      int i = nCount;
      while (i) {
        --i;
        if (ppSel[i] == pUnit)
          goto next;
      }
    }

    // Hyperion: or the replay viewer's slots past 7 (stage D)
    if (!(pUnit->pSprite->bVisibilityFlags & gdwLocalMask) && !ViewHxSpriteSeen(pUnit->pSprite))
      continue;
    if ((pUnit->dwFlags & 0x300) && !(pUnit->dwUnionE4 & gdwLocalMask) && !ViewHxDetected(pUnit))
      continue;
    if (usel_type_unselectable(pUnit->wType))
      continue;
    if (!USEL_IS_LOCAL_MULTISELECT(pUnit)) {
      pFallback = pUnit;
      continue;
    }

    // One short-circuit condition, so the append block is placed out of line.
    DWORD dwDelta;
    if (!pTarget
        || (pTarget->bPlayer == pUnit->bPlayer
            && pTarget->wType == pUnit->wType
            && !((dwDelta = pTarget->dwFlags ^ pUnit->dwFlags) & 0x10)
            && (unit_is_cloaked_by_arbiter(pUnit) || !(dwDelta & 0x100))
            && !(dwDelta & 0x40000000))) {
      if (nCount >= USEL_MAX_SELECT)
        sub_4BBDD0(ppSel, nCount, pUnit, pTarget);
      else
        ppSel[nCount++] = pUnit;
    }

  next:
    ;
  }

  if (!nCount && (pFallback || pTarget)) {
    ppSel[0] = pTarget ? pTarget : pFallback;
    nCount = 1;
  }
  // Hyperion: in a game of Hyperion's selection, where buildings select together, a box
  // over units and buildings takes the units, as StarCraft II's does
  if (SelectionWide() && !pTarget && nCount > 1) {
    int nUnits = 0;
    for (int i = 0; i < nCount; i++)
      if (!(gulUnitTypeFlags[ppSel[i]->wType] & 1))
        ppSel[nUnits++] = ppSel[i];
    if (nUnits != 0) {
      for (int i = nUnits; i < nCount; i++)
        ppSel[i] = NULL;
      nCount = nUnits;
    }
  }
  return nCount;
}


// 0x4BC2D0. The band-box gather: enumerate every unit under the drag rectangle,
// either merge them into the live selection, when the additive latch is set and a
// selection exists, or replace it, and show the rally point when the result is a
// single rallying building of the active nation. The one register parameter goes
// straight to find_units_in_rect.
void __fastcall usel_drag_select(rect16_t *pRect)
{
  CUnit *ppFound[USEL_SELECT_CAPACITY];   // [ebp-30h]
  int    nAdditive = byte_665E68;         // movzx into a full register, then test

  memset(ppFound, 0, sizeof(ppFound));

  CUnit **ppIter = find_units_in_rect(pRect);
  int nFound = sub_4BC5B0(ppIter, ppFound, NULL);
  finish_unit_iteration(ppIter);
  if (!nFound)
    return;

  if (nAdditive && NewUnits()[0]) {
    CUnit *ppMerged[USEL_SELECT_CAPACITY];  // [ebp-60h]
    // Hyperion: the live selection's 12 or 255, the rest null (stage S)
    memset(ppMerged, 0, sizeof(ppMerged));
    memcpy(ppMerged, NewUnits(), USEL_MAX_SELECT * sizeof(CUnit *));
    int nMerged = sub_4BC4C0(ppMerged, ppFound, nFound, NULL);
    sub_4BCE70(nMerged, ppMerged, 1, 1);
    return;
  }

  sub_4BCE70(nFound, ppFound, 1, 1);
  if (nFound == 1) {
    CUnit *pUnit = ppFound[0];
    if (pUnit && pUnit->bPlayer == g_ActiveNationID && pUnit->IsRallyer())
      ppFound[0]->DisplayRallyTarget();
  }
}

// ---------------------------------------------------------------------------
// the click-selection dispatcher
// ---------------------------------------------------------------------------
// Here sub_4BBDD0 stays out of line and sub_4BCE70 comes in, and the compiler
// reproduces the oracle's own per-site split for the latter without being forced:
// inlined at 2 of its 5 sites and a real call at the other 3. IsSelfOwned is a
// real call, for the reason given at its declaration above. Whether
// usel_type_unselectable is defined here does not matter to this body, checked
// both ways: usel_click_select forwards its coordinates in the registers they
// arrive in, with no setup at all, which is also the arity proof for its own
// signature.
//
// The memset and memcpy asymmetry is just what this compiler does at /Oi for a
// 48-byte constant size: memcpy inlines to three wide pairs while memset emits a
// real call. memmove is likewise a real call.


// Hyperion: the map pixels on screen, where Ctrl+click and a double click take every
// unit of the type clicked. 1.17 bands its view, 640 by 400; Hyperion's camera may show more
// or less of the map than the game's view (zoomed out, zoomed in, beside a HUD's columns), so
// the band is what the camera shows (PortViewSeenRect), and a double click takes the units
// the player sees at any zoom. At zoom 1 that is the game's view as before. Kept inside the
// map: zoomed far out the camera's sight is wider than the map, and a rect16 holds 32,767 at
// most. The units go out in the select command as ever, so replays and other players are
// given the same command whatever drew the band.
#ifdef OC_DLL_MODE
  #ifndef gwMapWidth
  #define gwMapWidth  (*reinterpret_cast<WORD *>(OC_VA(0x5264B4u)))
  #endif
  #ifndef gwMapHeight
  #define gwMapHeight (*reinterpret_cast<WORD *>(OC_VA(0x5264B6u)))
  #endif
#else
extern WORD gwMapWidth;
extern WORD gwMapHeight;
#endif

static void UselScreenRect(rect16_t *pRect)
{
  int nLeft, nTop, nRight, nBottom;
  PortViewSeenRect(&nLeft, &nTop, &nRight, &nBottom);
  const int nMapRight = gwMapWidth * 32;
  const int nMapBottom = gwMapHeight * 32;
  pRect->left   = (short)(nLeft < 0 ? 0 : nLeft > nMapRight ? nMapRight : nLeft);
  pRect->top    = (short)(nTop < 0 ? 0 : nTop > nMapBottom ? nMapBottom : nTop);
  pRect->right  = (short)(nRight < 0 ? 0 : nRight > nMapRight ? nMapRight : nRight);
  pRect->bottom = (short)(nBottom < 0 ? 0 : nBottom > nMapBottom ? nMapBottom : nBottom);
}

// 0x4BBE70. Resolve what the player clicked, then commit one of five outcomes
// depending on the Shift and Ctrl latches: plain single select, band-select the
// whole viewport for all of this type, merge into the live selection, add one
// unit to it, or remove an already-selected unit from it. A bare `ret` and two
// register parameters, forwarded to get_map_target as they arrive.
//
// The order of the locals matters: the compiler gives the first-declared local
// the slot closest to EBP, and the oracle's frame puts the target first, then the
// viewport rect, then the two 12-pointer arrays, adjacent and distinct.
void __fastcall usel_click_select(int nX, int nY)
{
  CUnit *pTarget;                          // [ebp-4]  -- address taken, so it
                                           //   lives in the frame and the store
                                           //   precedes the null test
  rect16_t rcView;                         // [ebp-0Ch]
  CUnit *ppSel[USEL_SELECT_CAPACITY];      // [ebp-3Ch]
  CUnit *ppMerged[USEL_SELECT_CAPACITY];   // [ebp-6Ch]

  // Both latches are read into locals before the call, and that order is proven
  // rather than chosen: the two loads sit ahead of it, and the compiler cannot
  // hoist a global load above a call that might store to it.
  int nAddKey    = byte_665E68;             // ebx
  int nToggleKey = byte_665E69;             // esi

  pTarget = get_map_target(nX, nY);
  if (!pTarget)
    return;

  // A branchy 0/1 materialisation of an `&&`, folded into the latch with |=.
  nToggleKey |= (pTarget->pSprite->flags.b & 8) && gbIsDoubleClickEvent;

  // A real call for the zeroing, where the 48-byte copy below inlines; see the
  // note at the head of this section.
  memset(ppSel, 0, sizeof(ppSel));

  if (!nToggleKey && (!nAddKey || !NewUnits()[0])) {
    // The plain single select. nCount is the literal 1, so the inlined
    // sub_4BCE70 here has no count compare: it is folded away.
    sub_4BCE70(1, &pTarget, 1, 1);
    goto rally;
  }

  if (nAddKey) {
    if (nToggleKey) {
      // Shift and Ctrl: band the viewport for every unit of this type, then
      // merge the result into the live selection.
      // Hyperion: the live selection's 12 or 255, the rest null (stage S)
      memset(ppMerged, 0, sizeof(ppMerged));
      memcpy(ppMerged, NewUnits(), USEL_MAX_SELECT * sizeof(CUnit *));
      // Hyperion: what the screen shows, 640 by 400 in 1.17 (UselScreenRect).
      UselScreenRect(&rcView);

      CUnit **ppIter = get_units_in_rect(&rcView);
      int nFound = sub_4BC5B0(ppIter, ppSel, pTarget);
      finish_unit_iteration(ppIter);
      if (!nFound)
        return;
      // Two statements and not one nested call, from the push order: arguments
      // are evaluated right to left, so a nested call pushes the two constants
      // before running it, where the oracle pushes them after it returns.
      int nMerged = sub_4BC4C0(ppMerged, ppSel, nFound, pTarget);
      sub_4BCE70(nMerged, ppMerged, 1, 1);
      return;
    }

    // Shift alone: copy the live selection out, then add or remove one unit. One
    // array is indexed and the other walked by pointer, as in the oracle, which
    // keeps nCount a single induction variable.
    int nCount;
    CUnit **ppLive = NewUnits();
    for (nCount = 0; nCount < USEL_MAX_SELECT; nCount++, ppLive++) {
      CUnit *pCur = *ppLive;
      if (!pCur)
        break;
      ppSel[nCount] = pCur;
    }

    // The arms are in the oracle's order, which puts the add path on the
    // fall-through and the remove path after its epilogue; the other way round
    // the two blocks swap and every forward branch changes width.
    if (!(pTarget->pSprite->flags.b & 8)) {
      // Not selected, so add it, if there is room and both ends agree.
      if (nCount >= USEL_MAX_SELECT)
        return;
      if (!USEL_IS_LOCAL_MULTISELECT(ppSel[0]))
        return;
      if (!pTarget->IsMultiSelectable())
        return;
      // A real call here, where the macro above inlines the same predicate; both
      // spellings are the oracle's. pTarget is copied into a local first, which
      // is what puts the load ahead of the call with the register reused for the
      // store, one value live across a call needing a nonvolatile register.
      CUnit *pAdd = pTarget;
      if (!IsSelfOwned(pAdd))
        return;
      ppSel[nCount] = pAdd;
      sub_4BCE70(nCount + 1, ppSel, 1, 1);
      return;
    }

    // Already selected, so remove it. bSelectionIndex is its slot and the memmove
    // closes the hole, the count decremented first so the difference is the tail.
    int nSlot = pTarget->pSprite->bSelectionIndex;
    --nCount;
    memmove(&ppSel[nSlot], &ppSel[nSlot + 1],
            (nCount - nSlot) * sizeof(CUnit *));
    ppSel[nCount] = NULL;
    sub_4BCE70(nCount, ppSel, 0, 1);   // a3 == 0 here, 1 everywhere else
    return;
  }

  // CTRL alone: replace the selection with everything of this type on screen.
  if (!nToggleKey)
    return;
  // Hyperion: what the screen shows, 640 by 400 in 1.17 (UselScreenRect).
  UselScreenRect(&rcView);

  CUnit **ppIter;  // Hyperion: set apart from its declaration, which a goto jumps past
  ppIter = get_units_in_rect(&rcView);
  int nFound;  // Hyperion: set apart from its declaration, which a goto jumps past
  nFound = sub_4BC5B0(ppIter, ppSel, pTarget);
  finish_unit_iteration(ppIter);
  if (!nFound)
    return;
  sub_4BCE70(nFound, ppSel, 1, 1);
  if (nFound != 1)
    return;

rally:
  // No null test on pTarget here -- unlike usel_drag_select's copy of this tail, it is
  // already known non-null.
  if (pTarget->bPlayer == g_ActiveNationID && pTarget->IsRallyer())
    pTarget->DisplayRallyTarget();
}

// Registered globals only usel_click_target reads.
#ifdef OC_DLL_MODE
  #ifndef gpStatUnit
  #define gpStatUnit (*reinterpret_cast<CUnit **>(OC_VA(0x6AB4C4u)))
  #endif
  #ifndef gpStatUnits
  #define gpStatUnits (reinterpret_cast<CUnit **>(OC_VA(0x6AB4C8u)))
  #endif
  #ifndef gubOrderAttack
  #define gubOrderAttack (reinterpret_cast<BYTE *>(OC_VA(0x51AE40u)))
  #endif
  #ifndef gubOrderWeapon
  #define gubOrderWeapon (reinterpret_cast<BYTE *>(OC_VA(0x51B740u)))
  #endif
  #ifndef order_techType
  #define order_techType (reinterpret_cast<BYTE *>(OC_VA(0x51B800u)))
  #endif
  #ifndef gubOrderThingyRemap
  #define gubOrderThingyRemap (reinterpret_cast<BYTE *>(OC_VA(0x51BC80u)))
  #endif
#else
extern CUnit *gpStatUnit;
extern CUnit *gpStatUnits[SELECTION_MAX];
extern BYTE gubOrderAttack[189] OC_UNALIGNED;
extern weapon_id_t gubOrderWeapon[189];
extern tech_id_t order_techType[189];
extern order_id_t gubOrderThingyRemap[189] OC_UNALIGNED;
#endif

// 0x4BC820 (860 B). Send the clicked order for the selected units, or play the
// most specific refusal. The attack path reaches the one send_net_cmd_Action call
// by goto; a second call there swaps four frame slots. Each energy select comes
// before the flag beside it, and bTech is read before the energy call.
void __fastcall usel_click_target(int nX, int nY, intptr_t nTarget, int nCard)
{
  if (!target_is_on())
    return;

  CUnit *pTarget = reinterpret_cast<CUnit *>(nTarget);
  DWORD dwErr = 0xFFFF;
  order_id_t bOrder = target_order((int)nTarget, nCard);
  BYTE bAttack = gubOrderAttack[bOrder];
  weapon_id_t bWeapon = gubOrderWeapon[bOrder];
  int nEnergyCost = get_order_tech_energy_cost(bOrder);
  tech_id_t bTech = order_techType[bOrder];
  BYTE bSpellHit = 0;
  BYTE bAnyCanAct = 0;
  if (gubOrderThingyRemap[bOrder] == 0xBD)
    nCard = 0xE4;
  BYTE bHasEnergy = 0;

  for (int i = 0; i < SelectionMax(); ++i) {  // Hyperion: selectlimits.h's (stage S)
    CUnit *pUnit = StatUnits()[i];
    if (pUnit == NULL || (pUnit->dwFlags & 0x400) || pUnit->bLockdownTimer ||
        pUnit->bStasisTimer || pUnit->bParalyzeTimer)
      continue;

    if (bAttack) {
      if (bWeapon == WEAPON_NONE) {
        if (pTarget == NULL) {
          if (pUnit->wType == 0x32 || pUnit->CanTargetGroundType(nCard))
            bAnyCanAct = 1;
        } else if (pUnit->GetRClickAction() == 3) {
          if (pUnit->sub_420AB0(pTarget))
            dwErr = 0x378;
          else if (pUnit->sub_420B50(pTarget))
            dwErr = 0x379;
          else
            bAnyCanAct |= pUnit->CanTarget(pTarget, 1);
        } else {
          bAnyCanAct |= pUnit->CanTarget(pTarget, 1);
        }
      } else if (pUnit->CanTargetWithWeapon(bWeapon, pTarget) || IsVulnerableType(nCard)) {
        bHasEnergy = pUnit->NeedsMoreEnergy(nEnergyCost) ? bHasEnergy : 1;
        bAnyCanAct = 1;
      }
    } else if (bTech != 0x2C) {
      if (pUnit->sub_434130(bTech, pTarget, nCard, nX, nY, reinterpret_cast<WORD *>(&dwErr))) {
        bHasEnergy = pUnit->NeedsMoreEnergy(nEnergyCost) ? bHasEnergy : 1;
        bSpellHit = 1;
      }
    } else if ((WORD)nCard == 0xE4) {
      pUnit->ErrorRClick(bOrder, pTarget, nX, nY, reinterpret_cast<WORD *>(&dwErr));
    } else {
      pUnit->ErrorLClick(bOrder, nCard, nX, nY, reinterpret_cast<WORD *>(&dwErr));
    }
  }

  if (bAttack) {
    if (!bAnyCanAct || (WORD)dwErr != 0xFFFF) {
      sub_4BCEB0(pTarget, bWeapon, reinterpret_cast<WORD *>(&dwErr));
      BYTE bPlayer = gpStatUnit->bPlayer;
      gamesnd_show_player_error(bPlayer, gpStatUnit->GetRace() + 0x8A, dwErr);
      if (bWeapon != WEAPON_NONE)
        return;
      goto send;
    }
  } else if (nEnergyCost != 0 && !bSpellHit) {
    DWORD dwNation = g_ActiveNationID;
    if ((WORD)dwErr == 0xFFFF)
      dwErr = 0x36C;
    gamesnd_error(gpStatUnit, status_get_str((WORD)dwErr), dwNation);
    return;
  }

  if (nEnergyCost != 0 && !bHasEnergy) {
    BYTE bPlayer = gpStatUnit->bPlayer;
    gamesnd_show_player_error(bPlayer, gpStatUnit->GetRace() + 0x9C,
                              (WORD)(gpStatUnit->GetRace() + 0x360));
    return;
  }
  if ((WORD)dwErr != 0xFFFF) {
    BYTE bPlayer = gpStatUnit->bPlayer;
    gamesnd_error(gpStatUnit, status_get_str((WORD)dwErr), bPlayer);
    return;
  }
  gamesnd_ack(gpStatUnit);
send:
  send_net_cmd_Action(nX, nY, pTarget, nCard, bOrder, byte_665E68);
}
