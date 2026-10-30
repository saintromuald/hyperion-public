// collide.h -- companion declarations for src117/lang/collide.cpp, the unit
// collision matrix: the per-cell flag words in gpSquareMap, the rect walks that
// set and clear them, and the placement and adjacency queries built on them.
//
// Member functions live in their class header; this header owns the TU's free
// functions, which is also what lets a generated hook row reach one. They are
// spelled without an explicit __fastcall, since /Gr already makes it the default
// and a redundant one makes the generator read the name as ambiguous.

#ifndef OC117_COLLIDE_H
#define OC117_COLLIDE_H

#include "platform.h"
#include "CUnit.h"
#include "point16.h"
#include "pos.h"      // Hyperion: spos_t

// The family contract, true of nearly every body here: the matrix is
// `gpSquareMap` (0x666454, one flag word per build cell) and a cell is addressed
// `gpSquareMap[gwMapWidth * y + x]`, with gwMapWidth loaded `movzx` -- a WORD, so
// it promotes unsigned -- and the coordinates loaded `movsx`, so they promote
// signed.

// -- the cell accessor -------------------------------------------------------
// 0x412C10. Bare `ret`; ECX and EDX are both read `movsx` 16-bit, so two short
// register arguments and no stack. Its one consuming caller takes the full EAX.
int mtx_flags(short sX, short sY);

// -- masked predicates -------------------------------------------------------
// 0x413270 and 0x413220. `ret 8h`; ECX and EDX read `movsx` 16-bit, then two
// 4-byte stack slots, and every `ret` materialises 0 or 1.
//
// The _px twin takes pixel coordinates and converts with the signed divide by 32
// that rounds toward zero, 32 pixels per build cell; the cell twin takes cell
// coordinates and does not.
BOOL mtx_has_flags(short sX, short sY, DWORD dwRequired, DWORD dwForbidden);
BOOL mtx_has_flags_px(spos_t sPixelX, spos_t sPixelY, DWORD dwRequired, DWORD dwForbidden);  // Hyperion: positions

// -- the forward rect walks --------------------------------------------------
// 0x412C40 and 0x4134A0. `ret Ch`; ECX and EDX read 32-bit, then three 4-byte
// stack slots. Void: EAX at the ret is inherited and no caller consumes it.
//
// One shape, two polarities: mtx_place_bldg ORs the mask in, mtx_unplace_bldg
// ANDs its complement out (`not edi` at the top). Both walk row-major from
// (x, y), advancing `gwMapWidth - nWidth` cells between rows.
void mtx_place_bldg(int nX, int nY, int nWidth, int nHeight, DWORD dwFlags);
void mtx_unplace_bldg(int nX, int nY, int nWidth, int nHeight, DWORD dwFlags);

// -- the reverse rect scans --------------------------------------------------
// 0x4134F0 and 0x413580. `ret Ch`; ECX and EDX read `movsx` 16-bit, then three
// stack slots of width short, short and BYTE, and both `ret` arms materialise 0
// or 1.
//
// Both scan the rect backwards, from (x + nWidth - 1, y + nHeight - 1) down to
// (x, y), and both build their mask as `1 << bBit` recomputed at the top of every
// row. They differ in polarity and in a bias:
//   sub_413580  all cells must have bit bBit set   (empty rect -> FALSE)
//   sub_4134F0  any cell has bit (bBit + 8) set    (empty rect -> FALSE)
// The `+8` is in the bytes (`add eax, 8h` right after the movzx), not inferred.
BOOL sub_4134F0(short sX, short sY, short sWidth, short sHeight, BYTE bBit);
BOOL sub_413580(short sX, short sY, short sWidth, short sHeight, BYTE bBit);

// -- the unit-type callback --------------------------------------------------
// 0x413370. Bare `ret`; ECX is dereferenced, so one pointer register argument.
// Its address is taken twice with no direct call site, so no caller fixes the
// return, but the whole body is `(gulUnitTypeFlags[wType] >> 13) & 1`, a
// materialised 0 or 1 that would be dead code at void.
BOOL is_resource_container_cb(CUnit *pUnit, CUnit *pContext);  // sub_442B60's callback shape; pContext unread

// -- the forwarder -----------------------------------------------------------
// 0x412C30. A frame set up and torn down around nothing, then `jmp sub_42ED50`.
// The body touches none of its arguments, but `ret 4h` and seven callers that all
// set ECX prove three of them passing straight through, and the tail jump pins
// the return type. The parameter names come from src117/h/CUnitZSpells.h, which
// declared it off the broodling call site -- evidence this body cannot supply.
BOOL mtx_passable(int nUnitType, spos_t x, spos_t y);  // 16-bit x/y, as sub_42ED50

// -- the placement queries ---------------------------------------------------
// A coordinate slot loaded as a dword and then narrowed is an int the body
// narrows into a short local, not a short parameter: mtx_query_place,
// sub_413150 and mtx_query_place_bldg pass it on unwidened to sub_412360, whose
// pixel centre shifts the raw dword. gUnitPlaceboxSize is in pixels and reaches
// these as build cells through the signed divide by 32, with the two halves
// packed into a point16_t passed by value as one dword.
int mtx_query_place(BYTE bPlayer, int nX, int nY, int nUnitType, int nIndex);
// sub_413150's unit type is a WORD: placebox_highlight_addons pushes it as the
// dword of a slot whose upper half was never written.
int sub_413150(CUnit *pUnit, BYTE bPlayer, int nX, int nY, WORD nUnitType);

// 0x4132B0. Walks get_units_at_point's list for a unit of wType. `ret 4h`, so two
// register parameters and one stack parameter: ECX is stored as a WORD, EDX and
// the stack slot are both consumed through `movsx`, and both exits load the
// return from a local, so it is a pointer. The call site passes the order target
// pair, which fixes the coordinate roles.
CUnit *sub_4132B0(WORD wType, spos_t sX, spos_t sY);

// 0x413390. The return type is a choice rather than a derivation: nothing in the
// corpus falsifies either width. The one caller's truncating store consumes AL at
// any width, so it says nothing, while the body materialises 0, 1 or 4 full-width
// through a `cmovne` an 8-bit return could not express, and the caller family
// compares the result as an int. Codegen is identical either way.
int mtx_unit_there(CUnit *pUnit, CUnit **ppList, int nX, int nY, BYTE bArg4,
                   BYTE bArg5);

// -- the psi-power query -----------------------------------------------------
// 0x4130F0. Every width here is measured: the type or either coordinate spelled
// `int` compiles several bytes short. The player is the exception -- the callee
// forwards it untouched -- and its width comes from the one call site, which
// loads only DL, leaving the rest of EDX undefined.
//
// The return is an error code and not a BOOL: the body is "powered ? 0 : 6" and
// the caller folds the result into its running placement-error code.
int mtx_query_powered(WORD wType, BYTE bPlayer, short sCellX, short sCellY);

// -- does this building block this build cell? ------------------------------
// 0x4122D0. ECX is dereferenced at three offsets, so it is a CUnit *, and both
// rets materialise 0 or 1. The body is eight conjuncts and one shared `return 1`
// rather than eight guards: written as guards the compiler cross-jumps them and
// hoists the shared block to the top, where the oracle puts `xor eax, eax / ret`
// first and the shared exit last.
BOOL sub_4122D0(CUnit *pUnit, int nCellX, int nCellY);

// -- two more placement queries ----------------------------------------------
// Both return an int that mtx_query_place_bldg consumes: `movzx eax, bl` at the
// ret is a 32-bit return, where a BYTE one would be `mov al, bl`. sub_412720's
// first stack slot is a BYTE player index, indexing gPlayerData at its 36-byte
// stride rather than being dereferenced.
int sub_412720(short sX, short sY, BYTE bPlayer, point16_t size, int nSlot,
               int nFlag);
int mtx_check_on_creep(short sX, short sY, BYTE bPlayer, point16_t size, int nSlot,
               int nFlag, int nFlag2);
int sub_413610(int nCellX, int nCellY, BYTE bPlayer, int nFlag);

// 0x412F10. The unit-type parameter has to be `int nType` with the five array
// subscripts written `[(WORD)nType]`, and that moves bytes rather than being a
// style choice: spelled WORD, the index and the value handed to the four callees
// are one value, so the compiler folds the scale into every addressing mode and
// each access grows a byte. Spelled int plus casts they are different values, and
// the oracle's own narrowing of the index is the tell that a cast is in the
// source. The player is 8-bit on caller evidence: both call sites pass the raw
// byte register, leaving the rest of EDX alone, which only happens when the
// parameter is a byte.
//
// C linkage because the callers disagree with the body about nType: the body
// reads an int, while CUnit::DispatchDroneLand pushes an unconverted WORD and
// matches only through a WORD view of its own. Both views decorate the same.
extern "C" int mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int nX,
                                    int nY, int nType, int nSlot, int nArg6,
                                    int nArg7, int nArg8);
// Parser-visible redeclaration for gen; it inherits the C linkage above.
int mtx_query_place_bldg(CUnit *pUnit, BYTE bPlayer, int nX, int nY,
                         int nType, int nSlot, int nArg6, int nArg7,
                         int nArg8);

// -- the three big placement callees, unported -------------------------------
// Seated here rather than in the .cpp so a generated hook row can resolve them.
// The signatures are derived from the caller side, so whoever lands these three
// re-derives them from the bodies and corrects anything that disagrees.

// 0x412870. `ret 14h`. ECX indexes gulUnitTypeFlags, so it is the unit type and
// not a pointer, and a WORD: sub_413150 hands its own WORD type on unwidened.
// EDX and the first stack slot are the cell x and y, 16-bit; the next is read
// `movzx edx, al`, a BYTE; the last is the packed placebox size.
int mtx_check_near(WORD nUnitType, short sX, short sY, BYTE bPlayer,
                   point16_t ptSize, int nArg5, int nArg6);

// 0x412360. `ret 20h`. ECX is forwarded as mtx_unit_there's ECX, so a CUnit *,
// and mtx_query_place passes NULL. EDX and the first stack slot are ints: the
// cell index narrows them, but the pixel centre shifts the raw dword, which a
// short parameter would have sign-extended. Then a BYTE player, a WORD unit type
// and the placebox size.
int sub_412360(CUnit *pUnit, int nX, int nY, BYTE bPlayer, WORD nUnitType,
               point16_t ptSize, int nArg6, int nArg7, int nArg8, int nArg9);

// 0x412560, 443 B. `ret 10h`. ECX `movzx esi, cx` -> the unit type. EDX
// `movsx eax, bx` and [ebp+8h] `movsx eax, dx` -> shorts. [ebp+0Ch] is never
// read by the callee (unfalsifiable; the caller passes the player byte).
int sub_412560(int nUnitType, short sX, short sY, BYTE bPlayer,
               point16_t ptSize, int nArg5);

// 0x412C90, 394 B, held in collide.cpp. ECX only, both exits materialise 0 or
// 1, and its one caller (sub_432420) truth-tests EAX -> BOOL.
BOOL sub_412C90(CUnit *pUnit);

#endif  // OC117_COLLIDE_H
