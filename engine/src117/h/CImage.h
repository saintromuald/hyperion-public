// CImage.h -- CImage, a sprite's image: its 64-byte layout, the flags word and colouring
// cell, the member functions, and the free functions and rle_draw_* family of
// src117/lang/CImage.cpp.
//
// Deliberately minimal and grown as bodies land, like src117/h/CSprite.h: the
// `_unmapped_*` arrays hold the underived remainder at its true size so the named fields
// land where 1.17 puts them, and the asserts at the bottom check that arithmetic.
//
// Two things are deliberately not defined here, both for one reason: an owned header that
// generated code includes must not carry avoidable definitions, since that code spans both
// trees and anything defined here has to survive meeting the reference tree's version of
// itself. The images.dat id constants live in src117/h/imagetypes.h, whose names would
// otherwise be duplicate enumerators, and the GROUP and FRAME struct bodies in
// src117/h/grp.h, a C2011 against GDS/gds.h the first time this header carried them.
#pragma once

#include "platform.h"   // BYTE / WORD / DWORD / ULONG, and <stddef.h> offsetof
#include "point16.h"    // point16_t, InitPlacemask's size
#include "pos.h"        // Hyperion: spos_t (stage E)
#include "typelimits.h" // Hyperion: iscript_off_t

// The GRP group header and its frame table. Forward declarations first, so the two
// pointer typedefs can sit above every struct definition in the file; that keeps them out
// of any struct's member scope, which is what the layout checker walks.

struct FRAME;
struct GROUP;
typedef GROUP *PGROUP;
typedef FRAME *PFRAME;

// This typedef has to stay up here with the forward declarations: image_update_ent below
// has an IMAGEUPDATEPROC member, and with the typedef further down the file the sizeof
// assert fires as a C2338 that reads like a layout refutation but is only the implicit-int
// cascade.
class CImage;
typedef void(__fastcall *IMAGEUPDATEPROC)(CImage *pImage);

// First parameter of the static CImage::AllocSelect only -- a pointer needs no
// definition. `class` (not `struct`) so the tag agrees with src117/h/CSprite.h,
// which MSVC otherwise diagnoses as C4099.
class CSprite;

// Return type of CImage::CreateThingy only -- a pointer needs no definition, and `class`
// agrees with src117/h/CThingy.h's own tag.
class CThingy;

// An iscript animation header. src117 only ever holds a pointer to one
// (get_image_script returns it and hands it straight back to its caller), so
// the contents are not derived and the type stays incomplete. Give it members
// when a wave first reads one.
struct iscript_anim_hdr_t;

// The 32-bit screen rectangle CImage::GetDimensions fills, defined in src117/h/rect32.h.
// A pointer parameter needs only the declaration, and the forward form keeps this header's
// include closure at platform.h alone. The one TU that declares a rect32_t object,
// src117/lang/CSprite.cpp, includes rect32.h directly.
struct rect32_t;

// The iscript cursor RunScript advances: CImage+0x10..+0x17, or a free-standing copy
// (CUnitPathUtil.cpp's sgPathScript_g). +0x04 is the `call` return offset. Hyperion:
// the three offsets are iscript_off_t, 32 bits on 64.
struct iscript_state_t {
  /*0x00*/ iscript_off_t wIscriptHeader;
  /*0x02*/ iscript_off_t wIscriptOffset;
  /*0x04*/ iscript_off_t wUnk04;
  /*0x06*/ BYTE bAnim;
  /*0x07*/ BYTE bWait;
};
OC_X86_LAYOUT(sizeof(iscript_state_t) == 0x08, "iscript_state_t size");
// Hyperion: the cursor's bytes in an image, 8 in 1.17, which its zeroing stores clear
#define ISCRIPT_STATE_BYTES (offsetof(iscript_state_t, bWait) + 1)

// The out-parameter of CImage::GetShieldOffset (oracle 0x40F6A0). Two dwords, both the
// width and the count read off that function's own tail: two `movsx` byte loads of an
// adjacent GRP pair, then `mov dword ptr [eax], ecx` and `mov dword ptr [eax+4h], edx`
// through the out pointer. The members are int-width even though every value that reaches
// them fits in a signed byte, so CSprite::AllocShield narrows them again before
// handing them to CImage::Initialize. Which member is which is derived at that call site:
// +0x00 becomes Initialize's third argument and +0x04 its fourth, so x then y. The name is
// descriptive and ours -- no sheet, IDB or symbol names this type.

// The two per-palette-type dispatch tables CImage::SetRLE (0x4116E0) reads to fill
// pfnUpdate (+0x38) and pfnRender (+0x34). Both strides come off the addressing mode and
// nothing else: `mov eax, dword ptr [esi*8+503044h]` gives sgUpdateFuncs an 8-byte entry
// with its member at +0x04, and `lea eax, [esi+esi*2]` then `[eax*4+5030D4h]` gives
// sgDrawFuncs a 12-byte entry with members at +0x04 and +0x08, the +0x08 arm taken when
// the image's flags bit 1 is set. CImage::DecloakInit reaches the identical displacements
// independently.
//
// The +0x00 member of each is unmapped on purpose: nothing reads it, it exists to place the
// members that are read, and naming it would be invention. pfnNormal and pfnFlipped are
// `void *` rather than typed procs for the same reason CImage::pfnRender is -- nothing
// calls through them, so their parameter lists are unproven. The extents are not claimed:
// the next sheet row bounds sgUpdateFuncs' storage at 0x90, storage is not a count, and an
// indexed load compiles identically at any extent.
struct image_update_ent {
  /*0x00*/ void *_unmapped_00;
  /*0x04*/ IMAGEUPDATEPROC pfnUpdate;
};
OC_X86_LAYOUT(sizeof(image_update_ent) == 8, "image_update_ent  *8 scale in `mov eax,[esi*8+503044h]`");
OC_X86_LAYOUT(offsetof(image_update_ent, pfnUpdate) == 0x04, "image_update_ent::pfnUpdate  503044h - 503040h");

struct image_draw_ent {
  /*0x00*/ void *_unmapped_00;
  /*0x04*/ void *pfnNormal;
  /*0x08*/ void *pfnFlipped;
};
OC_X86_LAYOUT(sizeof(image_draw_ent) == 12, "image_draw_ent  *12 scale via `lea eax,[esi+esi*2]` + [eax*4+...]");
OC_X86_LAYOUT(offsetof(image_draw_ent, pfnNormal)  == 0x04, "image_draw_ent::pfnNormal   5030D4h - 5030D0h");
OC_X86_LAYOUT(offsetof(image_draw_ent, pfnFlipped) == 0x08, "image_draw_ent::pfnFlipped  5030D8h - 5030D0h");

struct shield_offset_t {
  /*0x00*/ int x;
  /*0x04*/ int y;
};
static_assert(sizeof(shield_offset_t)      == 8,    "shield_offset_t     two dword stores at [eax] and [eax+4h] in CImage::GetShieldOffset");
static_assert(offsetof(shield_offset_t, x) == 0x00, "shield_offset_t::x  `mov dword ptr [eax], ecx`");
static_assert(offsetof(shield_offset_t, y) == 0x04, "shield_offset_t::y  `mov dword ptr [eax+4h], edx`");

// The GROUP and FRAME struct bodies are in src117/h/grp.h -- see the header comment above
// for why, and that file's own comment for the derivations. Only src117/lang/CImage.cpp
// includes it; nothing here needs more than the forward declarations and the two pointer
// typedefs above.

// CImage's two multi-width fields. Both are unions, and in both cases the union is derived
// rather than chosen for convenience: 1.17 accesses the same offset at two or three
// different widths, which a plain scalar field cannot produce. Every arm sits at offset 0
// by definition, so the only thing to check is the size, asserted below each one.

// The flags word at CImage+0x0C. Sixteen bits wide, and that matters: CImage::Show reads
// it `movzx eax, word ptr [ecx+0Ch]` and writes it back as a word, CImage::SetOffset sets
// a bit with `or word ptr [ecx+0Ch], 1h`, and SetRLE and FlipImage do the same pair. A
// byte field would lose the 16-bit store; a dword field would read four bytes and collide
// with the two offsets at +0x0E and +0x0F.
union image_flags_t {
  // The whole word. CImage::InitImages manipulates a pool entry's flags through this
  // width directly, as a run of `and cx, ...` and `or cx, 1h` on the pool base plus 0x0C.
  WORD w;
  // The bits. MSVC allocates same-type bitfields from the least significant bit up in
  // declaration order, so the unnamed runs below are what put Hidden on bit 6.
  struct {
    // bit 0 (0x0001) Dirty -- this image needs redrawing. Five independent bodies set it:
    // SetOffset and SetVerticalOffset where an offset actually changed, Show in its
    // read-modify-write, and SetRLE and FlipImage before storing the word back.
    WORD Dirty  : 1;
    // bits 1, 3 and 5. The names are a readability change and nothing depends on them:
    // a bitfield insert in 1.17 is not evidence of a bitfield in the source, which is the
    // inverse of the natural assumption. Measured on CImage::Initialize at a match, the
    // whole-word form
    //     flags.w = (WORD)((flags.w & 0xFFDF) | ((WORD)(img_clickable[n] & 1) << 5));
    // is byte-identical to `flags.b.Clickable = img_clickable[n] & 1;` -- the mask-then-or
    // shape is what produces 1.17's `and`/`or`/word-store, and the declaration is free.

    //   bit 1 (0x0002) Mirror -- the horizontal mirror, from two bodies: CImage::FlipImage
    //        derives it from its own argument with `shr al, 1h / and al, 1h`, and
    //        CImage::ClickableAt takes the mirrored arm on `test byte ptr [ecx+0Ch], 2h`.
    //        The name is descriptive and ours; the bit is derived.
    //   bit 3 (0x0008) GfxTurns   <- img_gfxTurns  @0x5126E8, `shl cx, 3h`
    //   bit 5 (0x0020) Clickable  <- img_clickable @0x512AD0, `shl ax, 5h`
    //        Both are images.dat flag columns copied in by CImage::Initialize, and the
    //        pairing is read off the instruction order in its own stream. The names are
    //        not coined: both addresses carry these exact spellings in
    //        sc-refs/sheets/data_naming_1.17.csv, so the only thing derived here is which
    //        bit each column lands in.

    // Bits 2 and 4 stay unnamed and are still real flags, not padding.
    WORD Mirror    : 1;
    WORD           : 1;
    WORD GfxTurns  : 1;
    WORD           : 1;
    WORD Clickable : 1;
    // bit 6 (0x0040) Hidden. CImage::Hide tests it `test byte ptr [esi+0Ch], 40h` and
    // sets it with an `or`; CImage::Show tests it the same way and clears it with
    // `and eax, 0FFBFh`.
    WORD Hidden : 1;
    // bits 7..15 -- underived remainder, held so the union is exactly 2 bytes.
    WORD        : 9;
  } b;
};
static_assert(sizeof(image_flags_t) == 2, "image_flags_t   `mov word ptr [ecx+0Ch], ax` in CImage::Show");

// The colouring cell at CImage+0x30: four bytes accessed at three widths, which is the
// whole reason it is a union and not a DWORD. CImage::SetRLEPlayer and CImage::Initialize
// write it as a dword; SetRLE and FlipImage both `mov word ptr [.+30h], 230h`, and a DWORD
// field assigned a constant would emit `mov dword ptr`, so that store proves sub-object
// access; a byte read and a byte write appear elsewhere in the family. The four-byte extent
// is fixed on the other side by +0x34, which SetRLE writes as its own dword.

// What it means is not derived beyond "the per-image colouring selector": SetRLEPlayer
// stores a player or colour index and SetRLE stores 0x230 for one palette type. The name
// is the runtime-verified layout's, `coloringData`.

// The byte-pair arm is derived twice over, independently of the one body that consumes it
// (CSprite::GetRLEInfo): CImage::InheritDrawInfo copies the two bytes across separately,
// so they are two distinct one-byte members and not one WORD, and rle_advance_cloak uses
// them as a step and wait pair -- `inc byte ptr [esi+30h]` against 8, and a countdown on
// +0x31. Those two names are descriptive and ours, unlike the sheet-sourced names
// elsewhere in src117/h.
union coloring_data_t {
  DWORD_PTR dw;   // the dword arm -- CImage::SetRLEPlayer, CImage::Initialize
  WORD  w;    // the word arm  -- CImage::SetRLE's `mov word ptr [.+30h], 230h`
  BYTE  b;    // the byte arm  -- CSprite::RemoveTeamCircle's `mov bl,[ecx+30h]`
  // The byte pair. layout_check.py does not walk union arms, so the offsets are stated
  // here and pinned by the sizeof assert below plus CImage's own +0x34 assert.
  struct {
    BYTE bRleFrame;   // +0x30
    BYTE bRleWait;    // +0x31
  } rle;
};
OC_X86_LAYOUT(sizeof(coloring_data_t) == 4, "coloring_data_t `mov dword ptr [ecx+30h], eax` in SetRLEPlayer");

// The per-image update procedure stored at CImage+0x38. CSprite::Update calls it
// indirectly -- `mov eax, dword ptr [esi+38h]` / `mov ecx, esi` / `call eax` -- so the
// image arrives in ECX and nothing is pushed: one register argument, no stack argument and
// no cleanup is __fastcall. The keyword is spelled on the typedef above because a function
// pointer's convention is part of its type and /Gr does not reach through a typedef the
// way it reaches a definition. The return type is not derived, CSprite::Update ignoring
// EAX, so `void`. CImage::DecloakInit corroborates the slot from the write side, storing
// it out of sgUpdateFuncs.

// CImage -- 64 bytes (0x40), proven five independent ways, which is worth spelling out
// because every offset below is only meaningful relative to it. CImage::GetImage converts
// a 1-based slot to a pointer with `shl ecx, 6h`, a *64 scale; and CImage::InitImages
// builds four free lists whose memset size divided by loop count is exactly 0x40 each,
// every walk advancing `add e<r>x, 40h`:
//         pool 0x544698   memset 0x4E200 (320000)  loop 0x1387+1 = 5000 entries
//         pool 0x5928A0   memset 0x300   (768)     loop `cmp esi,0Ch` = 12
//         pool 0x592C50   memset 0x1400  (5120)    loop 0x4F+1     = 80
//         pool 0x594070   memset 0x1000  (4096)    loop 0x3F+1     = 64
//
// No base class, for the same reason src117/h/CUnit.h has none: 1.17 proves offsets, not
// which of them a base contributed. Deriving the layout flat keeps this file free of the
// reference tree's headers and keeps CImage non-polymorphic and single-inheritance, which
// src117/gen/hook_table.cpp depends on -- it recovers a member function's code address by
// memcpy out of a pointer-to-member.
class CImage {
public:
  //   +0x00  pPrev     CSprite::RemoveTeamCircle walks the image list backwards from
  //   +0x04  pNext     CSprite::pImageTail advancing `mov ecx, dword ptr [ecx]`, so the
  //                    dword at +0x00 is the previous CImage*, and the forward advance in
  //                    every CSprite walk is `mov esi, dword ptr [esi+4h]`. That the pair
  //                    is an intrusive link rather than two unrelated pointers is derived
  //                    on the CSprite side: AllocImageBelow, AllocImageAbove and
  //                    ClaimOwnership all hand &sprite+0x1C and &sprite+0x20 together to
  //                    the CLink helpers as one list's head and tail.

  //   +0x08  wImageID  `movzx eax, word ptr [ecx+8h]` in CImage::SetScriptDefault,
  //                    immediately indexing img_iscriptId. Written by CImage::Initialize
  //                    as a word store and re-read there as a movzx. Two bytes, unsigned;
  //                    a DWORD field would emit a plain `mov` and lose SetScriptDefault.

  //   +0x0A  bPaletteType  an 8-bit load or compare in six bodies, and corroborated from
  //                    the write side by CImage::CloakInit's `mov byte ptr [ecx+0Ah], 2h`
  //                    and CImage::SetRLE's `mov byte ptr [ecx+0Ah], bl`. Unsigned is
  //                    discriminated exactly once, by CSprite::ShowNormal's two-sided
  //                    range test; the other guards are equality-only.

  //   +0x0B  bDirection  `movzx eax, byte ptr [ecx+0Bh]` in CImage::CalcDisplayNo: one
  //                    byte, unsigned. The write side is what makes it a direction rather
  //                    than an anonymous byte -- CImage::FaceDirection guards on it and
  //                    commits its own direction argument to it, and CImage::CreateOverlay
  //                    mirrors it across the 32 facings (`mov ecx, 20h` / `sub ecx, eax`)
  //                    and pushes the result straight into FaceDirection.
  /*0x00*/ CImage *pPrev;
  /*0x04*/ CImage *pNext;
  /*0x08*/ WORD wImageID;
  /*0x0A*/ BYTE bPaletteType;
  /*0x0B*/ BYTE bDirection;
  /*0x0C*/ image_flags_t flags;
  //   +0x0E  cHorzOffset  byte accesses in CImage::SetOffset and the +0x0F pair in
  //   +0x0F  cVertOffset  CImage::SetVerticalOffset, written by CImage::Initialize from
  //                    its third and fourth stack arguments, so one byte each. They are
  //                    signed, and that is derived rather than inherited from the `char`
  //                    in the argument list: CImage::CreateThingy, DrawUpdateRect and
  //                    RunScript all sign-extend them, where an unsigned field would be
  //                    read with movzx. A negative offset is what an overlay drawn above
  //                    or left of its parent needs.
  /*0x0E*/ char cHorzOffset;
  /*0x0F*/ char cVertOffset;
  // +0x10 is the iscript header offset: CImage::ForceScript ends `mov word ptr
  // [ebx+10h], ax` where AX is the second WORD of an {id, headerOffset} pair read out of
  // the iscript.bin id table, so the field is a WORD and it is that offset.

  // The six bytes after it are split field by field, and the split changes the bytes
  // rather than hygiene: through a byte array /Ox merges the three constant stores at
  // +0x14, +0x16 and +0x17 into one `mov dword ptr [esi+14h], 10000h`, where 1.17 keeps
  // them separate, and no statement order recovers the five bytes that costs
  // rle_advance_warp_fade. Widths and offsets come from three bodies carrying the same
  // block: CImage::SetAnimation and its two constant-folded inline copies in
  // rle_advance_warp_fade and CImage::InitPlacemask.

  // +0x16 is the animation id, and the displacement proves it: SetAnimation's table read
  // is `mov ax, word ptr [eax+ecx*2+8h]` with ECX the new anim, the warp-fade inline sets
  // anim 1 and the same read appears as `+0Ah`, and InitPlacemask sets anim 0 and it
  // appears as `+8h`. 8 + 1*2 = 0x0A, so the base is +8 into the script header and the
  // index is this field.

  // The name at +0x14 is not derived and is deliberately left positional. Its width is --
  // bodies store a WORD zero into it -- but nothing we have compiled reads it, since
  // CImage::RunScript takes the block by address and is unported. A descriptive name here
  // beats a plausible one.
  // Hyperion: the three offsets are iscript_off_t, 32 bits on 64
  /*0x10*/ iscript_off_t wIscriptHeader;
  /*0x12*/ iscript_off_t wIscriptOffset;  // `mov word ptr [esi+12h], ax` -- the script pointer SetAnimation resolves
  /*0x14*/ iscript_off_t w14;             // `mov word ptr [esi+14h], ax` (ax = 0) in all three bodies; name underived
  /*0x16*/ BYTE bAnim;           // `cmp byte ptr [esi+16h], 1h`; `mov byte ptr [esi+16h], al`
  /*0x17*/ BYTE bWait;           // `mov byte ptr [esi+17h], 0h`
  //   +0x18  wFrameSet    WORD, unsigned, from `movzx edx, word ptr [ecx+18h]` in
  //                    CImage::CalcDisplayNo. The width matters here: a DWORD field
  //                    emits a plain `mov` and loses the match outright. Corroborated in
  //                    three other bodies, one of them from the write side.

  //   +0x1A  wFrameIndex  WORD, unsigned, and CalcDisplayNo pins the extent from both
  //                    sides without leaving the one body: a movzx read and a word store.
  //                    Corroborated in six more. The two are adjacent and ordered, not
  //                    independently placed -- CalcDisplayNo derives one from the other,
  //                    and CImage::Initialize zeroes the pair with a single dword store.
  //                    That merged store is not read as evidence of a dword field, six
  //                    word-width loads saying otherwise, but whether it comes from /Ox
  //                    merging two constant stores or from a union arm is not derived.
  /*0x18*/ WORD wFrameSet;
  /*0x1A*/ WORD wFrameIndex;
  //   +0x1C  wMapX / +0x1E  wMapY   both loaded signed by CImage::GetDimensions --
  //                    `movsx eax, word ptr [edi+1Ch]` and the +0x1E pair -- and stored
  //                    into a rect32_t `long`. movsx fixes width at 2 and signedness at
  //                    signed in one instruction, stronger than the usual movzx evidence:
  //                    a WORD field feeding a long would have emitted movzx. The sign here
  //                    is genuinely proven, unlike CSprite's +0x14 -- read that file's
  //                    refutation before assuming the two are analogous.
  /*0x1C*/ spos_t wMapX;  // Hyperion: positions of 4 bytes on x64 (stage E)
  /*0x1E*/ spos_t wMapY;
  //   +0x20  wScreenX / +0x22 wScreenY   signed 16-bit screen coordinates.
  //                    CImage::Invalidate reads both with movsx and stores the results
  //                    straight into a rect32_t's `long left` and `long top`. A
  //                    sign-extending load at a site whose sign is live -- the value goes
  //                    into a signed long and is then added to -- discriminates the field,
  //                    unlike the sign-dead movzx that misled an early reading of
  //                    CSprite::wPosX. CSprite::ForceInvalid and HideCloaked repeat the
  //                    pair through their inlined copies, and CImage::DrawUpdateRect
  //                    corroborates offset and width from the write side with two-byte
  //                    stores. The `w` prefix is kept for consistency with the rest of the
  //                    tree although the field is signed, as src117/h/CSprite.h does for
  //                    wPosY.

  //   +0x24  wGrpX / +0x26 wGrpY   the GRP-source offset a clipped draw starts at, paired
  //                    with the clipped extent below: CSprite::Draw loads both with movsx
  //                    into the first two dwords of the rect32 it passes to pfnRender,
  //                    ahead of wDrawWidth and wDrawHeight in the second two.
  //                    DrawUpdateRect is the write side, two-byte stores pinning offset
  //                    and width. Signed by Draw's movsx feeding dword stores, and not
  //                    sign-dead: the values reach pfnRender at full width. The names are
  //                    descriptive and ours.

  //   +0x28  wDrawWidth / +0x2A wDrawHeight   unsigned 16-bit clipped extents.
  //                    CImage::Invalidate reads them zero-extending, which is what makes
  //                    the field unsigned, and then sign-extends the same 16 bits a second
  //                    time before adding them to the coordinates: the load and the sign
  //                    are two separate operations, an unsigned field read through a
  //                    signed 16-bit conversion. A `short` field would emit one movsx load
  //                    and reuse it. DrawUpdateRect pins the offsets from the write side
  //                    and says what they are: the GRP frame's byte width and height,
  //                    shifted by the viewport and clamped to 640 x 400. The names are
  //                    descriptive and ours.
  /*0x20*/ short wScreenX;
  /*0x22*/ short wScreenY;
  /*0x24*/ short wGrpX;
  /*0x26*/ short wGrpY;
  /*0x28*/ WORD wDrawWidth;
  /*0x2A*/ WORD wDrawHeight;
  //   +0x2C  pGroup   the type is derived rather than named: CImage::GetDimensions loads
  //                    the dword and then `lea esi, [eax+edx*8]` with EDX = wFrameIndex,
  //                    reading `[esi+8]` and `[esi+9]` -- an 8-byte stride indexed by
  //                    frame, landing on two adjacent bytes, which is exactly
  //                    GROUP::frames with +8 and +9 inside the element being bWidth and
  //                    bHeight. CImage::Kill nulls it, corroborating the width.
  /*0x2C*/ GROUP *pGroup;
  /*0x30*/ coloring_data_t coloringData;
  //   +0x34  pfnRender   from CImage::SetRLE, which writes it out of the sgDrawFuncs
  //                    table as a dword store. Deliberately not spelled as a typed
  //                    function pointer: pfnUpdate below is typed because the family that
  //                    fills it is derived, where nothing calls through +0x34, so a typed
  //                    signature would be a claim the evidence does not support. The width
  //                    is the whole finding.
  /*0x34*/ void *pfnRender;
  //   +0x38  pfnUpdate   `mov eax, dword ptr [esi+38h]` in CSprite::Update, and the
  //                    loaded dword is immediately the target of `call eax` with ECX the
  //                    image, so a four-byte code pointer and not data. See
  //                    IMAGEUPDATEPROC above for the convention.

  //   +0x3C  pParentSprite  CSprite::ClaimOwnership ends `mov dword ptr [esi+3Ch], edi`
  //                    where ESI is the image being re-homed and EDI the CSprite* `this`,
  //                    and the whole point of that function is to make this sprite the
  //                    image's owner. CImage::Initialize corroborates independently,
  //                    storing its second argument -- the owning sprite the four Alloc*
  //                    bodies pass as their own `this` -- to the same offset.
  /*0x38*/ IMAGEUPDATEPROC pfnUpdate;
  /*0x3C*/ CSprite *pParentSprite;

  // The member functions. Every one except GetImageFromGroupIndex is __thiscall: `this`
  // arrives in ECX and every declared argument is on the stack, and /Gr does not change
  // that for a non-static member. Argument counts are read off the callee's own `ret N`,
  // and the narrow types off how the body consumes the slot.

  // oracle 0x40F640 (8 B), whole body `mov eax, dword ptr [ecx*4+5436F8h] / ret`.
  //
  // Static, and 1.17 says so: ECX is scaled by 4 and used as a table index, never
  // dereferenced as an object, so it is a register argument and not `this`. 0x5436F8 is
  // sgpImgGroups, the per-image-type GRP pointer table, so the result is a GROUP*.
  //
  // The line break after `static` is deliberate: the signature generator reads a single
  // line and would restate `static` on a file-scope definition, which is C2724. Harmless
  // today only because this function is ported and so is never thunked.
  static
  PGROUP GetImageFromGroupIndex(ULONG id);

  // oracle 0x40F8B0 (669 B). Load the tileset shift bitmap, build the four free
  // lists. Static and nullary: neither call site loads ECX. Both discard EAX.
  static
  void InitImages();
  // Hyperion: the health bars and circles of a game of Hyperion's selection, 255 units
  // (stage S, h/selectlimits.h), as its selection starts (SelectionForGame).
  static
  void InitSelectionImagesWide();

  // oracle 0x40F620 (17 B). Static: ECX carries the 1-based slot under /Gr, and slot 0 is
  // NULL. `shl ecx, 6h` is the *0x40 pool stride and `lea eax, [ecx+544658h]` resolves
  // &sgImages[nImage - 1], the base being sgImages minus one stride -- a phantom address,
  // never to be recorded as a global, the same fold CSprite::GetSprite documents.
  static
  CImage *GetImage(DWORD nImage);

  // oracle 0x40F650 (20 B). Static, the inverse: 0 for NULL, else
  // ((DWORD)pImage - 0x544698) >> 6 plus 1. The shift is `shr`, an unsigned divide, so the
  // subtraction must be computed in an unsigned 32-bit temporary and not as a CImage*
  // pointer difference, which divides signed.
  static
  DWORD GetImageID(CImage *pImage);
  // oracle 0x40F000 (242 B) / 0x40F100 (191 B). The image-pool save codecs, the CImage
  // side of CSprite::EncodeListForSave and DecodeListFromSave. Nullary void: Decode seeds
  // ESI from the pool constant without reading ECX, Encode opens a frame and never reads
  // it, and both end in a plain `ret`. Static: their callers set no ECX.
  static void EncodeListForSave();
  static void DecodeListFromSave();

  // oracle 0x410090 (284 B), unported. ECX is the save file; the caller also
  // loads the save version into EDX, as it does for CBullet::LoadBullets,
  // though this body never reads it. The caller tests the full EAX.
  static
  BOOL LoadImages(void *pFile, WORD wVersion);
  // oracle 0x411300 (347 B), unported. ECX is the save file; the caller tests
  // the full EAX.
  static
  BOOL SaveImages(void *pFile);

  // oracle 0x4117D0 (34 B). `ret 8h` => two dword slots, and the body reads
  // each as a byte (`mov dl, byte ptr [ebp+8h]`, `mov al, byte ptr [ebp+0Ch]`)
  // and compares it against cHorzOffset / cVertOffset, which are signed -- so
  // `char`, matching the fields.
  void SetOffset(char x, char y);

  // oracle 0x411800 (23 B). `ret 4h` => one dword slot, read as a byte and
  // compared against cVertOffset. This is SetOffset with the horizontal arm
  // folded away, which is what /Ox does when the caller passes the field's own
  // current value.
  void SetVerticalOffset(char y);

  // oracle 0x411820 (21 B). Bare `ret`, `this` in ECX, nothing else set up =>
  // __thiscall with no arguments. Clears Hidden and sets Dirty in one
  // read-modify-write of the flags word.
  void Show();

  // oracle 0x40F890 (21 B). Bare `ret`, no arguments. Invalidates the current
  // rect (only when not already hidden), then sets Hidden.
  void Hide();

  // oracle 0x40EF60 (17 B). Bare `ret`, no arguments. Reloads this image's
  // images.dat default iscript through ForceScript.
  void SetScriptDefault();

  // oracle 0x4116D0 (16 B). `ret 4h` => one dword slot; the body narrows it
  // itself (`mov eax, dword ptr [ebp+8h]` / `movzx eax, al`) before the store,
  // which is what a BYTE parameter in a dword slot looks like -- do not widen
  // the parameter to chase the movzx.
  void SetRLEPlayer(BYTE bPlayer);

  // oracle 0x40F340. Declared only -- src117 has not authored the body, so it resolves
  // through the naked thunk in src117/gen/oracle_thunks.cpp, which restates this exact
  // signature, and a wrong one here is a compile error rather than a silently mismatched
  // mangled name. `ret 4h` => exactly one dword stack argument, and the body compares it
  // whole against a zero-extended WORD, so it is int-width and not WORD. It walks the
  // iscript id and offset pair table for that id and stores the matching offset to
  // CImage+0x10.
  void ForceScript(int id);

  // oracle 0x4164C0 (81 B). Bare `ret` and `this` in ECX. Builds a rect from
  // CImage+0x20/+0x22 and +0x28/+0x2A and calls invalidate_rect with it.
  //
  // Defined in src117/lang/CSprite.cpp and not in CImage.cpp, which is 1.17's own
  // arrangement: 0x4164C0 sits inside CSprite.cpp's address range, and CSprite::ForceInvalid
  // and CSprite::HideCloaked both inline this body, which needs it in their TU.
  // CImage::Hide's own out-of-line copy still contains a real call to it, which is
  // consistent rather than contradictory: that one lives in CImage.cpp's TU, where this
  // definition is not visible.
  //
  // Membership is the part that holds, not definition order: three separate experiments
  // -- a class member, a free function and a whole TU whose own comment predicted
  // otherwise -- produced byte-identical objects when a definition was moved past its
  // caller, MSVC deferring codegen to end-of-TU so the inliner's candidate set is the whole
  // TU. What WPO-off buys is that the callee has to be in the TU at all.
  void Invalidate();

  // Declared only, called from src117/lang/CSprite.cpp. Each of these resolves through
  // the naked thunk in src117/gen/oracle_thunks.cpp, which restates the signature, so a
  // wrong signature here is a compile error rather than a silently mismatched mangled
  // name. A wrong calling convention is not: it surfaces as an objdiff callee mismatch,
  // and that is why AllocSelect's derivation below is spelled out at length.

  // oracle 0x40ED00. Bare `ret` and `this` dereferenced straight out of ECX at
  // entry => __thiscall, no arguments. Sets bPaletteType to 2.
  void CloakInit();

  // oracle 0x411580, `ret 4h`. Int-wide, and derived rather than assumed: the
  // body reads the whole slot with `mov eax, dword ptr [ebp+8h]`, not a byte,
  // which is what lets CSprite::SetFacing hoist the whole dword into EDI.
  void SetFacing(int nDirection);

  // oracle 0x411690 (50 B). `ret 4h` => one dword slot, and the slot is `int` rather than
  // WORD on the same discrimination SetFacing carries: the body compares it whole against
  // a zero-extended wFrameSet and only narrows it afterwards. A WORD parameter would have
  // made that compare 16-bit.
  void SetFrame(int nFrameSet);

  // oracle 0x40F5A0 (47 B). `this` is dereferenced out of ECX at entry
  // (`mov ecx, dword ptr [ecx+3Ch]`, pParentSprite). `ret 4h` => one dword slot, and it is
  // `int` because the body reads it whole and never narrows it -- it goes straight into a
  // __fastcall ECX. Nothing is returned.
  void PlaySnd(int nSound);

  // oracle 0x40F5D0 (73 B). `ret 0Ch` => three dword slots, and GetShieldOffset
  // (0x40F6A0) is the same function one table over, with an identical two-dword
  // out-parameter tail, so it writes the same shield_offset_t and not a new type.

  // bLoType is a BYTE, and the spelling that proves it is counter-intuitive: 1.17 reads it
  // `mov eax, dword ptr [ebp+0Ch]` then `movzx eax, al`, a full-slot load followed by a
  // mask, which reads like an int being truncated. It is not -- the landed, byte-matched
  // rle_draw_warp_fade has `BYTE bLevel` and emits exactly that pair, because the BYTE
  // promotes to int in the arithmetic and MSVC loads the whole slot to do it. nOverlay is
  // read whole and scaled by 2 into the LO table, so int.

  // It returns pOff, and for a return the call sites are the authority:
  // CSprite::GetTwinSpawnOffsets and CSprite::DamageOverlaysUp all read `[eax]` and
  // `[eax+4h]` after the call, and EAX is caller-saved, so a void callee would leave
  // nothing readable there. The callee corroborates -- its tail loads pOff into EAX before
  // the two out-stores. The earlier `void` went unnoticed because the bytes are identical
  // at either spelling and every earlier call site discards the result.
  shield_offset_t *GetOverlayOffset(shield_offset_t *pOff, BYTE bLoType,
                                    int nOverlay);

  // The oracle callees of the two allocator free functions below. Both are __thiscall on
  // the newly allocated CImage, which is the opposite of their callers:
  // AllocHealthbar and AllocSelect are __fastcall with the CSprite as the first argument,
  // while these take the fresh image in ECX and the sprite as an ordinary argument.
  // Getting that pair backwards is a wrong-class bug the byte gate cannot see, since it
  // de-classes both sides. HealthbarInit: AllocHealthbar keeps the sprite in EDI and the
  // new image in ESI, then `mov ecx, esi / push edi / call` -- one dword argument, `this`
  // in ECX.
  void HealthbarInit(CSprite *pSprite);

  // SelectInit: AllocSelect forwards its own three parameters unchanged -- nImageID
  // pushed, then its EDX argument (bSelIndex), then its ECX one (pSprite), with the new
  // CImage in ECX -- so the parameter types are exactly AllocSelect's, which this header
  // already derives. Not derived: SelectInit's own `ret N`. The arity above is read off the
  // call site alone, which is sound here because the pushes are unambiguous, but it is not
  // the callee's own testimony.
  void SelectInit(CSprite *pSprite, BYTE bSelIndex, int nImageID);

  // Four declarations, each derived from its own body's instruction stream.

  // oracle 0x40EF80 (108 B). `ret 4h`, `this` in ECX, and the one dword slot is a CImage:
  // the body reads the source image's bPaletteType, immediately range-checked 2..7, and
  // the coloringData byte pair at +0x30 and +0x31. Neither arm sets EAX, so void. This is
  // the body the banner above describes as copying the two bytes across separately.
  void InheritDrawInfo(CImage *pSrc);

  // oracle 0x40F810 (128 B). `ret 4h`, `this` in ECX. Distinct from CImage::HealthbarInit
  // (0x40F6E0, 304 B) declared above -- different address, different body, different
  // arity; the ledger spells this one InitHPBar, and confusing the two is a wrong-class
  // bug the byte gate cannot see. The slot is read whole and scaled by 14, so it is an
  // int-width index into an array of 14-byte GROUPs and not a narrower bar id.
  void InitHPBar(int nBarType);

  // oracle 0x411840 (134 B). `ret 4h`, `this` in ECX, no frame pointer.
  //
  // The parameter is dead, and that is 1.17's bug and not ours: nothing in the body reads
  // the slot, the iscript id handed to CImage::ForceScript being the literal 0xC1, while
  // the one caller -- CUnit::DispatchBuildSelfProtoss -- pushes 0xD2. So the caller's
  // argument is computed, passed and ignored. `ret 4h` is what proves the slot exists at
  // all; declaring the function nullary would change the convention. Reproduced, not
  // repaired -- register it in docs/open-issues.md if a behavioural difference is ever
  // traced to it.
  void WarpFlashInit(int nImageID);

  // oracle 0x410320 (81 B). `ret 10h` => four dword slots, `this` in ECX.
  //
  //   arg0  only the low word reaches CThingy::AllocThingy, whose landed signature takes
  //         an `int` first parameter, so the WORD is a property of this body and not a
  //         reason to narrow AllocThingy.
  //   arg1  added onto the movsx'd cHorzOffset  -- int.
  //   arg2  added onto the movsx'd cVertOffset  -- int.
  //   arg3  a byte load stored into the new thingy's own sprite. That load reads the same
  //         for BYTE and int, so the one caller decides: RunScript pushes the unnarrowed
  //         sum and matches only with an int.
  //
  // It returns the thingy: the tail is `mov eax, esi`, and the early arm falls out with
  // ESI still NULL, so both exits return the pointer.
  CThingy *CreateThingy(WORD wType, int relX, int relY, int bElevation);

  // oracle 0x4116E0, `ret 4h` => one dword slot holding a palette type the body stores
  // straight into bPaletteType.
  //
  // The parameter is a BYTE, not an int, and the body discriminates it: 1.17's load is
  // `mov bl, byte ptr [ebp+8h]` where an int parameter emits `mov ebx, dword ptr
  // [ebp+8h]`, MSVC loading a parameter at the natural width of its declared type. That
  // one byte was the last delta on CImage::SetRLE and it is not steerable from the body --
  // a truncation to a local, a re-read through the stored field and a direct byte read of
  // the slot all fold back onto the dword load. Of the three call sites, two push a
  // zero-extended byte and only the third pushes a literal.
  //
  // Unsigned, not `char`: the index is formed `movzx esi, bl`, where a signed char would
  // emit movsx. The width is part of the mangled name, so changing it regenerates the
  // thunks.
  void SetRLE(BYTE bRLE);

  // oracle 0x40F1E0. `ret 4h` => one slot, and the width is the callee's own read:
  // `mov cl, byte ptr [ebp+8h]`, a direct byte load, the exact discrimination the SetRLE
  // note above records. Nothing sets EAX on any exit, so void. CSprite::Duplicate's call
  // site corroborates the width but is sign and width dead.
  void FlipImage(BYTE bFlip);

  // oracle 0x411730, `ret 4h` => one dword slot consumed as a BYTE. Signedness is not
  // derived -- the only use is an equality compare, which is sign-blind -- and BYTE is from
  // the callee comparing the argument against `byte ptr [esi+16h]`, the one-byte animation
  // id.
  //
  // `__declspec(noinline)` matters, and the price was measured at zero. CImage::InitScript
  // calls this body and only matches its 141 bytes if /Ob2 does not splice it; without the
  // attribute InitScript is 176. It used to look mutually exclusive with
  // rle_advance_warp_fade and CImage::InitPlacemask, which need the body's constant-folded
  // copy present, but those two now carry the folded block textually, so nothing is left
  // for the inliner to decide. SetAnimation's own 145 bytes match either way. The attribute
  // is repeated on the definition in src117/lang/CImage.cpp.
  __declspec(noinline) void SetAnimation(BYTE bAnim);

  // oracle 0x40F570 (45 B). Bare `ret` => no arguments; the call site leaves
  // the image pointer in ECX => __thiscall. CSprite::RemoveTeamCircle ignores
  // EAX afterwards, so `void` is what the call site needs.
  void RemoveCircle();

  // DrawUpdateRect: oracle 0x40EE70 (217 B), bare `ret` => no arguments and
  // `mov edx, ecx` at entry => __thiscall. FaceDirection: oracle 0x40F1C0
  // (192 B), `ret 4h` => one dword slot, hoisted whole by
  // CSprite::FaceDirection.
  void DrawUpdateRect();
  // The parameter is a BYTE and only the declaration can say so. `contracts.py show` reads
  // one byte at the slot off 1.17, and for a parameter the callee is the authority. The
  // byte-pointer lever that works on a field does not work here: `(BYTE)nDirection` and
  // `*(const BYTE *)&nDirection` both compile to the dword load, because taking a
  // parameter's address makes its stack slot addressable and MSVC then loads the declared
  // width. Only this spelling gives the byte load. The seven landed callers in CSprite.cpp
  // were measured, not assumed: 81/83 match in both arms with all 83 COMDATs
  // byte-identical, including CSprite::FaceDirection, whose caller forwards a full dword.
  // `ret 4h` is unchanged -- a BYTE parameter still occupies one pushed dword slot.
  void FaceDirection(BYTE bDirection);

  // oracle 0x40EC90 (98 B). Not __thiscall, and 1.17 says so from both sides. The call
  // site in CSprite::AllocSelect leaves ECX holding the CSprite* `this`, sets only DL and
  // pushes one dword; the callee reads ECX as a CSprite, reads EDX, reads the stack slot
  // and ends `ret 4h`. That is __fastcall with two register parameters and one stack
  // parameter, so a static member -- a __thiscall member would have to take a CImage* in
  // ECX, and it demonstrably does not. The second parameter is BYTE-typed because the
  // caller sets DL alone, never EDX. The line break after `static` is the
  // GetImageFromGroupIndex one.
  static
  CImage * __fastcall AllocSelect(CSprite *pSprite, BYTE bSelIndex,
                                  int nImageID);

  // Declared only, called from src117/lang/CSprite.cpp -- the same arrangement as the
  // block above: each resolves through the naked thunk, which restates the signature, so a
  // wrong signature here is a compile error, while a wrong calling convention surfaces
  // only as an objdiff callee mismatch. Argument counts are the callee's own `ret N`.

  // oracle 0x40EBA0. Static and takes no argument, and both halves are read off the pair
  // of sides: CSprite::AllocImage reaches it with ECX never loaded for it and nothing
  // pushed, and the callee's own body opens `push esi` / `mov esi, [0x544694]` without
  // reading ECX at all. A declaration taking a register parameter would make MSVC emit a
  // `mov ecx, ...` 1.17 does not have.
  //
  // It returns a CImage *: the four Alloc* bodies do `call` / `mov esi, eax`, test ESI for
  // null, then use it as the ECX `this` of three CImage members. src117/lang/CImage.cpp
  // now defines this body and it matches, so it is a hook row rather than a thunk, and the
  // declaration is unchanged -- the caller-side derivation was confirmed from the callee
  // side. The line break after `static` is the GetImageFromGroupIndex one.
  static
  CImage * __fastcall AllocImage();

  // oracle 0x40FC50. `ret 10h` => four dword slots. Argument 2 is the owning sprite: the
  // four Alloc* bodies push their own `this` into that position and the callee stores it
  // into pParentSprite.
  //
  // Arguments 3 and 4 are `char`, and that is derived. The discriminator is
  // CSprite::AllocShield, the only caller that does not simply forward its own parameter
  // slots: it narrows both values into one-byte locals and then hands them over as whole
  // dwords. Pushing four bytes of a one-byte object is only legal when the callee's
  // parameter is itself one byte wide -- the upper three bytes are then unspecified and
  // the callee reads only AL, which is what this body does. With `int` parameters MSVC must
  // widen at the call site instead, and it does, two bytes over 1.17's 130.
  //
  // It costs the other three callers nothing: they pass int-width values, and an int
  // argument to a char parameter is truncated implicitly, so MSVC pushes the caller's dword
  // slot unchanged.
  void Initialize(int nImageID, CSprite *pSprite, char x, char y);

  // oracle 0x40FB50. Bare `ret`, reached with `mov ecx, esi` only => __thiscall
  // with no arguments.
  void InitScript();

  // oracle 0x40ED30 (88 B). Bare `ret`, `this` dereferenced straight out of ECX
  // at entry (`cmp byte ptr [ecx+0Ah], 4h`) => __thiscall, no arguments. It is
  // also the write side of pfnUpdate -- see that field.
  void DecloakInit();

  // oracle 0x40F540 (45 B). Bare `ret`, `mov esi, ecx` at entry => __thiscall,
  // no arguments.
  void RemoveHPBar();

  // oracle 0x40FE10 (52 B). Bare `ret`, `mov esi, ecx` at entry => __thiscall,
  // no arguments. It unlinks the node it is given, so CSprite::Kill's walk saves pNext
  // before the call.
  void Kill();

  // oracle 0x40FDC0 (74 B). `ret 8h` => two dword slots, and the body
  // subtracts each from a sign-extended field (`movsx eax, word ptr [ecx+1Ch]`
  // / `sub edi, eax`, and the +0x1Eh pair), so both are signed int-width screen
  // coordinates. The result is whatever frame_clickable_at leaves in EAX and
  // CSprite::IsClickablePt tests it whole (`test eax, eax`), so `int`.
  int ClickableAt(int x, int y);

  // oracle 0x411530 (67 B). `ret 4h` => one dword stack slot, and the body
  // reads it as a pointer (`mov edx, dword ptr [ebp+8h]` then `mov dword ptr
  // [edx], eax` and three more stores at +4/+8/+0Ch) => an out-parameter.
  // `this` arrives in ECX (`mov edi, ecx`) => __thiscall. Four dwords in
  // left/top/right/bottom order is rect32_t.
  void GetDimensions(rect32_t *prc);

  // oracle 0x410480 (3426 B). `ret 0Ch` => three dword slots, which is exactly what
  // CSprite::Update pushes. The first is the address of CImage+0x10; the other two are
  // `push 0h` literals whose types nothing discriminates, so `int` is a placeholder and a
  // pointer would compile identically. Named a2 and a3 rather than guessed at.
  void RunScript(void *pIscript, int a2, intptr_t a3);

  // oracle 0x40F6A0. `ret 8h` => two dword slots. The first is the out pointer
  // (see shield_offset_t above for the two dword stores that fix its layout);
  // the second is read whole (`mov eax, dword ptr [ebp+0Ch]`) and scaled by 2
  // into a table, so it is int-width. CSprite::AllocShield forwards its own
  // `nDirection` parameter into it, which is what the name records; the table
  // lookup itself is not derived beyond that.
  void GetShieldOffset(shield_offset_t *pOff, int nDirection);

  // oracle 0x40EB60 (61 B). Not __thiscall, on exactly the evidence AllocSelect above
  // spells out: CSprite::SelectAdd reaches it with the CSprite `this` in ECX and pushes
  // nothing, and the callee's own body reads ECX as a CSprite -- `mov edi, ecx` then
  // `lea ecx, [edi+1Ch]`, CSprite::pImageHead. Bare `ret`, so no stack argument: __fastcall
  // with one register parameter, a static member.
  //
  // It takes the sprite, and that correction came from the callee's own side rather than
  // the caller's: a no-argument declaration also compiles at the call site, where ECX
  // happens to hold `this`, so only the body's two instructions settle it.
  //
  // It returns a CImage *: the body unlinks the healthbar free-list head and ends
  // `mov eax, esi`, or `xor eax, eax` when the pool is empty. CSprite::SelectAdd discards
  // it. The line break after `static` is deliberate.
  static
  CImage * __fastcall AllocHealthbar(CSprite *pSprite);

  // Defined in src117/lang/CImage.cpp.

  // oracle 0x40EE50 (28 B). __thiscall with no arguments, from both sides: `this` is
  // dereferenced straight out of ECX at entry with nothing else set up, the `ret` is bare,
  // and the sole call site reaches it with `mov ecx, ebx` and pushes nothing.
  //
  // It recomputes the GRP frame to draw -- the animation's frame set plus the facing
  // within it -- and, only when it moved, records the new one and sets flags.Dirty.
  // CImage::CreateOverlay carries an inlined copy of the body that is
  // instruction-for-instruction identical modulo the base register, which corroborates
  // every field this function reads.
  void CalcDisplayNo();

  // Reached by src117/lang/placebox.cpp, not defined by us.

  // oracle 0x40F490 (174 B). Static, and the bytes say so: no register input, no stack
  // parameters and a plain `ret`, so there is no `this` to read and the thiscall, cdecl and
  // stdcall alternatives are byte-identical. Its two callers, placebox_off and sub_47D530,
  // both reach it with nothing in ECX. It returns nothing, and both callers discard. Same
  // `static`-on-its-own-line spelling as AllocHealthbar above.
  static
  void ClearPlaceboxes();

  // oracle 0x40F3A0 (124 B). `ret 4h`, one byte-wide slot; CImage::RunScript
  // discards EAX. Toggles flags bit 7 and, when set, re-seats the special offset.
  void sub_40F3A0(BYTE bEnable);

  // oracle 0x4101B0 (366 B). `ret 14h`, `this` in ECX. RunScript pushes the
  // parent sprite as the second slot (never read) and zero-extended bytes for
  // x/y; the fifth selects PrependLink on the head over AppendLink on the tail.
  CImage *CreateOverlay(int nImageID, CSprite *pSprite, BYTE x, BYTE y,
                        BOOL bAbove);

  // The placebox constructors.

  // oracle 0x410380 (64 B). __thiscall with NO arguments: the body opens
  // `mov eax, dword ptr [ecx+3Ch]` with nothing read off a frame (there is no
  // frame -- no `push ebp`), and ends on a bare `ret`. All three callers
  // (CImage::FaceDirection, CUnit::AddInventory, CUnit::AddInventory_CUnit)
  // discard EAX, so void.
  void ApplySpecialOffset();

  // oracle 0x40ED90 (177 B). `ret 0Ch` => three dword slots, `this` in ECX. dwPos is a
  // packed DWORD, read once and shifted; size is a point16_t by value, and 1.17 re-reads
  // its slot at every use, which a DWORD parameter would CSE into one load. The halves go
  // to frames[0].bWidth and bHeight and to wMaxWidth and wMaxHeight.
  //
  // Void, and contracts.py says otherwise for a known reason: it reports a return because
  // the body's last statement is a call to CImage::RunScript and it never touches the
  // result. That is the documented tail-forwarding blind spot -- contracts.py output is a
  // floor. The one caller discards.
  void InitPlacemask(CSprite *pSprite, DWORD dwPos, point16_t size);

  // oracle 0x40EBD0 (191 B). `ret 8h` with both ECX and EDX live-in, so a static member
  // taking two register arguments and two stack ones; a __thiscall member would have to
  // read a CImage * in ECX and it demonstrably takes a CSprite * there.

  //   ECX  pushed as CImage::Initialize's pSprite and used as the sprite image-list head,
  //        so a CSprite *.
  //   EDX  `mov di, dx`, a sixteen-bit move, which is what makes the WORD real
  //        rather than cosmetic; later `movzx eax, di` indexes sgpImgGroups, and the sole
  //        caller sets it with a WORD load.
  //   [ebp+08h]  the packed position, handed whole to InitPlacemask and handed to
  //        CImage::Initialize as its two `char` arguments, and that is why 1.17 emits
  //        `push dword ptr [ebp+0Ah]` beside `push dword ptr [ebp+8h]`: for a `char` taken
  //        from offset +2 of a packed dword MSVC pushes the dword containing that byte and
  //        lets the callee use AL, so the read deliberately straddles into the next slot.
  //   [ebp+0Ch]  the unit type, `int` because the caller pushes a full dword from its own
  //        dword parameter. contracts.py also flags this slot written at +0Ch and +0Eh:
  //        that is MSVC reusing the dead parameter home as scratch for the packed size it
  //        builds for InitPlacemask, not a second parameter.

  // It returns a CImage *, and the caller consumes EAX. The line break after `static` is
  // deliberate.
  static
  CImage * __fastcall AllocPlacebox(CSprite *pSprite, WORD wGroupIndex,
                                    DWORD dwPos, int nUnitType);
};

// Every offset asserted above, checked at compile time: these emit no code, and a later
// promotion out of an `_unmapped_` array that gets the arithmetic wrong stops the build
// here instead of silently moving fields.
OC_X86_LAYOUT(sizeof(CImage)                  == 0x40, "CImage size          (shl ecx,6h in GetImage; four 0x40 pool strides in InitImages)");
static_assert(offsetof(CImage, pPrev)         == 0x00, "CImage::pPrev         [ecx]     -- CSprite::RemoveTeamCircle's backward walk");
OC_X86_LAYOUT(offsetof(CImage, pNext)         == 0x04, "CImage::pNext         [esi+4h]  -- the forward advance in every CSprite walk");
OC_X86_LAYOUT(offsetof(CImage, wImageID)      == 0x08, "CImage::wImageID      [ecx+8h]");
OC_X86_LAYOUT(offsetof(CImage, bPaletteType)  == 0x0A, "CImage::bPaletteType  [esi+0Ah] -- byte load; byte store in CloakInit/SetRLE");
OC_X86_LAYOUT(offsetof(CImage, bDirection)    == 0x0B, "CImage::bDirection    [ecx+0Bh] -- movzx in CalcDisplayNo; byte store in FaceDirection");
OC_X86_LAYOUT(offsetof(CImage, flags)         == 0x0C, "CImage::flags         [ecx+0Ch]");
OC_X86_LAYOUT(offsetof(CImage, cHorzOffset)   == 0x0E, "CImage::cHorzOffset   [ecx+0Eh]");
OC_X86_LAYOUT(offsetof(CImage, cVertOffset)   == 0x0F, "CImage::cVertOffset   [ecx+0Fh]");
OC_X86_LAYOUT(offsetof(CImage, wIscriptHeader) == 0x10, "CImage::wIscriptHeader [ebx+10h] -- `mov word ptr [ebx+10h], ax` in CImage::ForceScript");
OC_X86_LAYOUT(offsetof(CImage, wIscriptOffset) == 0x12, "CImage::wIscriptOffset [esi+12h] -- word store in SetAnimation and both its inline copies");
OC_X86_LAYOUT(offsetof(CImage, w14)            == 0x14, "CImage::w14            [esi+14h] -- word zero store x3; NAME UNDERIVED");
OC_X86_LAYOUT(offsetof(CImage, bAnim)          == 0x16, "CImage::bAnim          [esi+16h] -- indexes the script header's +8 anim table; +8 at anim 0, +0Ah at anim 1");
OC_X86_LAYOUT(offsetof(CImage, bWait)          == 0x17, "CImage::bWait          [esi+17h] -- byte zero store");
OC_X86_LAYOUT(offsetof(CImage, wFrameSet)     == 0x18, "CImage::wFrameSet     [ecx+18h] -- movzx word in CalcDisplayNo/FaceDirection");
OC_X86_LAYOUT(offsetof(CImage, wFrameIndex)   == 0x1A, "CImage::wFrameIndex   [ecx+1Ah] -- movzx word read AND `mov word ptr [ecx+1Ah], dx` write");
OC_X86_LAYOUT(offsetof(CImage, wScreenX)      == 0x20, "CImage::wScreenX      [eax+20h] -- movsx in Invalidate; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wScreenY)      == 0x22, "CImage::wScreenY      [eax+22h] -- movsx in Invalidate; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wGrpX)         == 0x24, "CImage::wGrpX         [esi+24h] -- movsx in CSprite::Draw; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wGrpY)         == 0x26, "CImage::wGrpY         [esi+26h] -- movsx in CSprite::Draw; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wDrawWidth)    == 0x28, "CImage::wDrawWidth    [eax+28h] -- movzx in Invalidate; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wDrawHeight)   == 0x2A, "CImage::wDrawHeight   [eax+2Ah] -- movzx in Invalidate; word store in DrawUpdateRect");
OC_X86_LAYOUT(offsetof(CImage, wMapX)         == 0x1C, "CImage::wMapX         [edi+1Ch] -- movsx in GetDimensions (SIGNED)");
OC_X86_LAYOUT(offsetof(CImage, wMapY)         == 0x1E, "CImage::wMapY         [edi+1Eh] -- movsx in GetDimensions (SIGNED)");
OC_X86_LAYOUT(offsetof(CImage, pGroup)        == 0x2C, "CImage::pGroup        [edi+2Ch] -- GetDimensions indexes frames at stride 8; Kill nulls it");
OC_X86_LAYOUT(offsetof(CImage, coloringData)  == 0x30, "CImage::coloringData  [ecx+30h]");
OC_X86_LAYOUT(offsetof(CImage, pfnRender)     == 0x34, "CImage::pfnRender     [ecx+34h] -- SetRLE's dword store out of sgDrawFuncs");
OC_X86_LAYOUT(offsetof(CImage, pfnUpdate)     == 0x38, "CImage::pfnUpdate     [esi+38h] -- CSprite::Update's `call eax`");
OC_X86_LAYOUT(offsetof(CImage, pParentSprite) == 0x3C, "CImage::pParentSprite [esi+3Ch] -- ClaimOwnership; Initialize's `mov [edx+3Ch], ecx`");

// The free functions that belong to this TU. All are defined in src117/lang/CImage.cpp,
// and the declarations are here so src117/gen/hook_table.cpp can take their addresses.

// oracle 0x40F680 (20 B). No arguments (`call IsBroodwar` and nothing else is
// set up), and the two results it selects between are the immediates 0x2F3 and
// 0x2F3+0xF4 -- 755 and 999, the vanilla and Brood War images.dat row counts.
// EAX is a full-width count, so `int`.
int GetNumImages();

// oracle 0x40F670 (9 B), whole body: `mov eax,[0x544018] / add eax,6h / ret`.
// 0x544018 is not a global of its own: it is &sgpImgGroups[584]
// (0x5436F8 + 584*4 = 0x920), the constant index folded into the absolute
// address by /Ox. The `add eax,6h` is GROUP::frames, so the result is a FRAME*.
PFRAME GetJuiceFrame();

// oracle 0x40FD50 (106 B). `ret 4h` with both ECX and EDX live-in, so
// __fastcall(ECX, EDX, one stack dword), and the two call sites agree on the roles:
// CImage::ClickableAt and cunit_protoss_init each set ECX to the FRAME * that indexes the
// group's frame table, EDX to an x, and push a y. It returns 32 bits -- ClickableAt takes
// EAX whole, and cunit_protoss_init truncates it at the store, which a BOOL return permits
// at either width.
BOOL frame_clickable_at(PFRAME pFrame, int x, int y);

// oracle 0x40FEF0 (79 B). Declared here because a hooked free function has to be.
// src117/lang/CImage.cpp defines it as a bare `void LoadGraphics()` at namespace scope,
// and that TU compiles /Gr, so the definition is already __fastcall and both spellings
// mangle alike. The `__fastcall` is written explicitly anyway: this header is also included
// by translation units that do not compile /Gr, where a bare declaration would be __cdecl
// and therefore a different mangled name -- a two-spellings LNK2019, invisible to the byte
// gate. The sibling TU-local prototypes for LoadGroups and init_damage_overlays carry the
// token for the same reason, the generator's resolver being unable to lift a prototype
// without one.
void __fastcall LoadGraphics();

// LoadGroups is a hooked row too, so it needs its seat in a header; nullary, bare `ret`.
void __fastcall LoadGroups();

// oracle 0x40EF50 (11 B), whole body:
// `mov eax,[0x54330C] / add eax,4h / add eax,ecx / ret`. ECX carries the one
// argument (__fastcall under /Gr for a free function) and is added as a raw
// BYTE offset into the script pool, so it is an int-width byte offset rather
// than a scaled index. The two separate adds are the pool header's `data`
// member (+4) and then that offset.
iscript_anim_hdr_t *get_image_script(int offset);

// Hyperion: the offset of animation nAnim in the script header at offset header, 0 for
// none. 1.17's headers hold a WORD each; a mod's, past 1.17's iscript.bin, a DWORD.
iscript_off_t ImageScriptAnim(iscript_off_t header, int nAnim);

// oracle 0x40FD30 (30 B). ECX is read as a CUnit (byte at +0x4C is bPlayer);
// both callers test the dword result.
class CUnit;
BOOL __fastcall IsSelfOwned(CUnit *pUnit);

// oracle 0x4103C0 (179 B). Nullary, plain `ret`; CImage::LoadImages discards EAX.
void __fastcall sub_4103C0();

// The rle_draw_* dispatch thunks. Four are defined in src117/lang/CImage.cpp; the other
// four are the real blitters they forward to, which are not ported and resolve through the
// naked thunks in src117/gen/oracle_thunks.cpp. Both kinds are declared here rather than as
// TU-local prototypes, and that placement earns its keep twice over:
// src117/gen/hook_table.cpp names `&fn` for every ported free function and hard-errors on a
// declaration it can only find in a .cpp, and the emitted thunk for an unported callee
// restates the signature at file scope -- with FRAME and rle_clip_t in its argument lists,
// it needs a header that declares those types to include.

// The family ABI, derived once and not re-derived per function. Every member is
// `__fastcall (r1, r2, a3, a4, a5)` -- two register arguments plus three dword stack slots,
// `ret 0Ch`. The proof is a call site rather than a prologue: rle_draw_playerside sets
// rle_draw_normal up with three pushes right to left while ECX and EDX arrive in registers,
// and the callee reads exactly those three slots and ends `ret 0Ch`. Corroborated from the
// other side by rle_draw_cloak, which calls two of these in a row and has to spill ECX and
// EDX to do it: a function preserving them across a call it did not need to make is a
// function whose callee takes them as arguments.

// a1 and a2 are the destination x and y -- rle_draw_fade adds a1 to the surface base and
// multiplies a2 against the pitch -- and a5 is a BYTE remap table in the general family, a
// 256-entry lookup indexed by the destination pixel. The explicit __fastcall is
// documentation: /Gr is in this TU's flag vector, so the keyword mangles identically, but a
// wrong convention surfaces only as an objdiff callee mismatch.

// a4 -- the source-rect and clip block. Its layout is deliberately unmapped: rle_draw_fade
// reads a4 at +0, +4, +8 and +0xC (source skip, start row, width, row count), which is
// enough to type it if anything here needed to, and none of the ported thunks touches the
// slot. Give it members when a wave lands one of the blitters.
struct rle_clip_t;

// The unported blitters: declared only, thunked.

// rle_draw_effect (0x4067F6, 346 B) and rle_draw_effect_hflip (0x406F03, 353 B) will never
// be C++, and their own addresses say so: neither is even 4-aligned, and both store into a
// run of slots at 0x501121..0x50113E that other members of the family also write, which
// re/globals117.json files under the TU `rle.asm`. Hand-written assembly with
// self-modifying operand slots, so an extern declaration is the end state here.

// The parameter shapes, only as far as the instruction stream commits to them: ECX is added
// straight to the destination pixel pointer, an x offset; EDX is multiplied by the surface
// pitch before being added to the same pointer, a y offset; the first stack slot is
// dereferenced at +4 only and the result walked as a WORD array; the second at +0, +4, +8
// and +0xC, all four dwords stashed into the self-modifying slots; the third is stored
// whole and used with a 16-bit index, the 64 KB remap table base. The middle two are not
// derived past "a 32-bit pointer", hence `void *`, and nothing about the thunks' bytes
// depends on refining them.
void __fastcall rle_draw_effect(int nX, int nY, void *pGraphic, void *pDraw,
                                void *pRemap);
void __fastcall rle_draw_effect_hflip(int nX, int nY, void *pGraphic,
                                      void *pDraw, void *pRemap);

// rle_draw_fade (0x4066A1, 341 B) and rle_draw_fade_hflip (0x407064, 348 B) -- the same
// five slots and the same `ret 0Ch`. a3 is typed here where the pair above is not, and that
// is not an inconsistency: these two dereference it as the GRP frame header, a3 at +4 being
// the frame's line-offset table, which src117/h/grp.h already names FRAME.
void __fastcall rle_draw_fade(int x, int y, FRAME *pFrame, rle_clip_t *pClip,
                              BYTE *pRemap);
void __fastcall rle_draw_fade_hflip(int x, int y, FRAME *pFrame,
                                    rle_clip_t *pClip, BYTE *pRemap);

// The ported thunks, defined in src117/lang/CImage.cpp.

// oracle 0x40E7C0 (24 B) and 0x40E7E0 (24 B). Forward to rle_draw_effect and
// rle_draw_effect_hflip with the cloak remap table substituted for the caller's own fifth
// argument.

// The fifth parameter is dead and must still be declared: `ret 0Ch` cleans three stack
// dwords while only two are ever read, so declaring four would emit `ret 8` -- three bytes
// short and a corrupted stack at every call site. What the dropped argument means is
// legible from rle_draw_cloak, the sibling that keeps it: it forwards the slot to
// rle_draw_uncloaked_part, which writes the partial-cloak stage byte. "Fully cloaked" has
// no partial stage, hence the drop.

// The rest of the dispatch layer follows, all of it sharing that one signature. What proves
// the legs take the same five arguments, rather than three stack ones and no registers, is
// the spill: each thunk saves ECX to the frame and EDX to EBX before its first call and
// reloads both before its second, where a __stdcall helper taking three stack arguments
// would not clobber either and would need no spill. They are declared here rather than left
// TU-local because the generator can only emit a thunk for a callee whose declaration it
// finds on the include path, and a guessed signature mangles wrong and fails at link.

// The family's shared second leg (0x406235 / its hflip twin): every dispatch
// thunk forwards all five of its own arguments to one of these unchanged.
void __fastcall rle_draw_uncloaked_part(int nX, int nY, void *pGraphic,
                                        void *pDraw, DWORD dwArg);
void __fastcall rle_draw_uncloaked_part_hflip(int nX, int nY, void *pGraphic,
                                              void *pDraw, DWORD dwArg);

// The plain blitters (0x405E1E / 0x4071C0) and the invisibility pass (0x4063B5). The `_asm`
// and trailing-underscore spellings are our clean-room map's names, not a description of
// how anything here is written. The fifth parameter is spelled `BYTE *pRemap` on these four
// and `DWORD` on the pair above, which is not sloppiness: the remap-table pointer is what
// was actually derived here, where the uncloaked_part leg only ever receives its caller's
// opaque fifth argument unexamined. Both spell one pushed dword, so no byte-match
// distinguishes them, and each name says what its own evidence supports.
void __fastcall rle_draw_normal(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);
void __fastcall rle_draw_hflip_(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);
void __fastcall rle_draw_invis_asm(int nX, int nY, void *pGraphic, void *pDraw,
                                   DWORD dwArg);
void __fastcall rle_draw_invis_hflip_asm(int nX, int nY, void *pGraphic,
                                         void *pDraw, DWORD dwArg);

// The warp-flash mask pass (0x405F64 / 0x4060C3), composited over the private
// texture buffer rle_draw_texture builds.
void __fastcall rle_mask_warp_flash(int nX, int nY, void *pGraphic, void *pDraw,
                                    BYTE *pRemap);
void __fastcall rle_mask_warp_flash_hflip(int nX, int nY, void *pGraphic,
                                          void *pDraw, BYTE *pRemap);

// oracle 0x41D740. Installs the active unit colour that rle_draw_playerside saves and
// restores around its blit.
//
// The parameter is an int, not a BYTE, and the callee misleads about it: it opens
// `movzx ecx, cl`, which reads as a BYTE parameter, and declaring it BYTE makes MSVC hand
// the value over in CL alone, leaving both playerside bodies four bytes short. With `int`
// the caller widens and both land. A callee re-normalising its own ECX is defensive codegen
// on its own side; only the caller's widening arbitrates.
void __fastcall sub_41D740(int nColor);

// The ported bodies themselves. src117/gen/hook_table.cpp takes the address of each, so a
// definition in the .cpp is not enough (C2065).
void __fastcall rle_draw_invis(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwArg);
void __fastcall rle_draw_invis_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwArg);
void __fastcall rle_draw_cloak(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwArg);
void __fastcall rle_draw_cloak_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwArg);
// These three take a BYTE, not a DWORD, and the difference is real: it is part of the
// mangled name, so getting it wrong is an LNK2019 against our own definitions, which is
// exactly how it was caught. The fifth argument here is a value the thunk consumes itself
// -- a player colour, a selection type -- not the opaque remap pointer the rest of the
// family forwards to its blitter.
void __fastcall rle_draw_playerside(int nX, int nY, void *pGraphic, void *pDraw,
                                    BYTE bColor);
void __fastcall rle_draw_playerside_hflip(int nX, int nY, void *pGraphic,
                                          void *pDraw, BYTE bColor);
void __fastcall rle_draw_select(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE bSelectType);
void __fastcall rle_draw_shift(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwShift);
void __fastcall rle_draw_shift_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwShift);
void __fastcall rle_draw_texture(int nX, int nY, void *pGraphic, void *pDraw,
                                 DWORD dwFrame);
void __fastcall rle_draw_texture_hflip(int nX, int nY, void *pGraphic,
                                       void *pDraw, DWORD dwFrame);

void __fastcall rle_draw_cloaked(int nX, int nY, void *pGraphic, void *pDraw,
                                 DWORD dwCloakStage);
void __fastcall rle_draw_cloaked_hflip(int nX, int nY, void *pGraphic,
                                       void *pDraw, DWORD dwCloakStage);

// oracle 0x40EB20 (27 B) and 0x40EB40 (27 B). A pure argument rewrite in front of
// rle_draw_fade and rle_draw_fade_hflip: turn the caller's warp level into the address of
// that level's 256-byte row inside the "ofire" light table, then tail-jump into the real
// blitter with the incoming frame reused untouched.
//
// The BYTE parameter is derived, not a convenience: 1.17 opens `mov eax, dword ptr
// [ebp+10h]` / `movzx eax, al`, not `movzx eax, byte ptr [ebp+10h]`. That is what a BYTE
// parameter in a dword slot looks like here -- do not widen it and add a cast to chase the
// movzx. CImage::SetRLEPlayer, landed and byte-matched, is `void SetRLEPlayer(BYTE)` and
// opens with the identical two instructions.
void __fastcall rle_draw_warp_fade(int x, int y, FRAME *pFrame,
                                   rle_clip_t *pClip, BYTE bLevel);
void __fastcall rle_draw_warp_fade_hflip(int x, int y, FRAME *pFrame,
                                         rle_clip_t *pClip, BYTE bLevel);

// rle_draw_greenbox, the three rle_advance_* update procs, and init_damage_overlays. All
// five are declared here and not as TU-local prototypes, for the two reasons the banner
// above gives: src117/gen/hook_table.cpp names `&fn` for every ported free function and
// hard-errors on a declaration it can only find in a .cpp, and the generator's resolver
// needs a header-reachable declaration carrying an explicit calling-convention token --
// without one it falls through the include roots into the quarantined tree.

// oracle 0x40E9D0 (140 B). A full member of the family ABI derived above -- `ret 0Ch`, two
// register arguments plus three dword slots. a3 is a FRAME, its bWidth and bHeight read at
// +2 and +3, a4 the clip block, and a5 is dead: it must still be declared, or `ret 0Ch`
// becomes `ret 8h` and the function is three bytes short.
void __fastcall rle_draw_greenbox(int nX, int nY, FRAME *pFrame,
                                  rle_clip_t *pClip, BYTE bLevel);

// oracle 0x40E570 / 0x40E5F0 / 0x40E650. IMAGEUPDATEPROCs, from both sides:
// each takes one register argument and no stack argument, opens `mov esi, ecx`
// and ends on a bare `ret`, and CImage::CloakInit / CImage::DecloakInit install
// exactly this family into CImage::pfnUpdate out of sgUpdateFuncs -- so the
// signature is the typedef's, including the void return.
void __fastcall rle_advance_cloak(CImage *pImage);
void __fastcall rle_advance_decloak(CImage *pImage);
void __fastcall rle_advance_warp_fade(CImage *pImage);

// oracle 0x40FBE0 (101 B). Nullary and void: no register input, no stack parameters and a
// plain `ret`, and there are no oracle callers, so every convention is byte-identical here
// and the __fastcall token is carried only to satisfy the generator's resolver.
void __fastcall init_damage_overlays();

// light_t -- Blizzard's light.cpp table element (light_g: light_t[7] @0x50883C). The layout
// is derived in src117/lang/CImage.cpp (base 0x50883C and not 0x508840: the .data image
// settles it), and it is header-visible so the registered global can be declared with its
// type.
struct light_t {
  /*0x00*/ DWORD dwId;        // 1..7, ascending in .data
  /*0x04*/ BYTE *pData;       // light_free `mov eax,[esi]` / SMemFree / null
  /*0x08*/ char  szName[12];  // sub_466810 `lea eax,[ebx+4h]` -> psnprintf %s
};
OC_X86_LAYOUT(sizeof(light_t)           == 0x14, "light_t size     light_free `add esi,14h`, 7 elements ending at 0x5088C8");
static_assert(offsetof(light_t, dwId)   == 0x00, "light_t::dwId    base 0x50883C is 0x508840 - 4");
OC_X86_LAYOUT(offsetof(light_t, pData)  == 0x04, "light_t::pData   light_free/sub_466810 base 0x508840");
OC_X86_LAYOUT(offsetof(light_t, szName) == 0x08, "light_t::szName  sub_466810 `lea eax,[ebx+4h]`, ebx = 0x508840");

// The interior names 1.17's code reaches two elements' pData by, as the fields
// they are: light_t's layout changes at 64 bits.
// The fifth light is the cloak colour-remap block prepare_visible_cloak_draw
// fills and the cloaked draws read. A TU using them declares light_g.
#define light_g_0_pData (light_g[0].pData)
#define dword_508890    (light_g[4].pData)

// 0x40F420 (104 B). Nullary, tail-jumping gamedata_free_data. No direct
// caller: RunGame takes its address and hands it to app_add_exit.
void group_free_all();
