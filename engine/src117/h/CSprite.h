// CSprite.h -- CSprite, the drawable a CUnit hangs its images off, its flags byte, and the
// free functions of src117/lang/CSprite.cpp.
//
// Deliberately minimal and grown as bodies land, like src117/h/CUnit.h: it carries only
// the fields the tree compiles today, and the `_unmapped_*` arrays hold the underived
// remainder at its true size so the named fields land where 1.17 puts them. The asserts at
// the bottom are the machine check on that arithmetic.
//
// cpp-1.17/lang/CSprite.h also exists, and MSVC resolves a quoted include from the
// including file's own directory first, so a file in src117/gen gets this header and one in
// cpp-1.17/lang gets theirs. That is why AllocImageAbove is declared below: with fields
// only, the payload build failed C2039 while the byte gate stayed green.
#pragma once

#include "platform.h"   // ours: BYTE/WORD + <stddef.h> for offsetof
#include "pos.h"        // Hyperion: upos_t, spos_t (stage E)

// Every CSprite member below that touches an image needs only a pointer, so a forward
// declaration is enough here; src117/lang/CSprite.cpp includes src117/h/CImage.h for the
// definition.
// Hyperion: the draw order's sort key (CSprite::GetSpritePriority, Priority.cpp): 1.17's
// 32 bits, or on 64 bits a key of 64 for a game past 1.17's limits, whose sprite indexes
// and rows pass 1.17's 13 bits each.
#if defined(_M_IX86)
typedef DWORD sprite_key_t;
#else
typedef unsigned long long sprite_key_t;
#endif
// Hyperion: a sprite's index in the pool (CSprite::wIndex), 16 bits in 1.17 and 32 on 64
// bits, where a game's sprites can pass 65,535.
#if defined(_M_IX86)
typedef WORD sprite_index_t;
#else
typedef DWORD sprite_index_t;
#endif

class CImage;

// Likewise for the 32-bit screen rectangle CSprite::GetSpriteRect fills and
// invalidate_rect consumes, both of which take it by pointer. It is defined in
// src117/h/rect32.h, which src117/lang/CSprite.cpp includes, three of its bodies
// declaring a rect32_t object.
struct rect32_t;

// The 8-byte {x, y} pair CImage::GetOverlayOffset fills. Defined in
// src117/h/CImage.h; GetTwinSpawnOffsets takes two by pointer.
struct shield_offset_t;

// The flags byte at CSprite+0x0E. One byte wide, and most consumers use the whole-byte
// arm; CSprite::Init pins it as a single byte adjacent to +0x0D, initialising the pair with
// one `mov word ptr [esi+0Dh], 4h` and then overwriting +0x0E alone.
//
// The bitfield arm is derived, not chosen for convenience: CSprite::ReleaseTeamCircle reads
// bits 1..2 with `shr al,1h` / `test al,3h` -- an extract -- and writes them back with
// MSVC's bitfield insert idiom `x ^ ((x ^ new) & mask)`, which a plain byte field cannot
// produce. CSprite::RemoveTeamCircleIf corroborates the same two bits from the other side.
union sprite_flags_t {
  // The whole byte. Bit 0 is the "has a selection image" flag AllocSelect tests and sets,
  // bit 3 is a separate flag and bit 4 is the one GetSpritePriority folds into the
  // draw-order key. None is named: naming a bit ahead of a body that needs it is how a
  // header gets ahead of its evidence.
  BYTE b;
  // The bits. MSVC allocates same-type bitfields from the least significant bit up in
  // declaration order, so the unnamed run below is what puts nTeamCircleTTL on bits 1..2.
  struct {
    BYTE                : 1;   // bit 0  -- AllocSelect's "already selected"
    BYTE nTeamCircleTTL : 2;   // bits 1..2 -- ticked down by ReleaseTeamCircle
    BYTE                : 5;   // bits 3..7 -- underived remainder
  } bits;
};
static_assert(sizeof(sprite_flags_t) == 1, "sprite_flags_t  `mov byte ptr [ecx+0Eh], al` in CSprite::ReleaseTeamCircle");

// CSprite -- the drawable a CUnit hangs its images off. 36 bytes (0x24), from
// CSprite::InitSprites striding the pool `add esi, 24h` over 0x9C3 elements; a pool walk's
// own stride is the object size.
//
// `class` and not `struct` is not a style choice: src117/h/CUnit.h forward declares
// `class CSprite;`, and MSVC diagnoses a tag mismatch, so the two spellings have to agree.
class CSprite {
public:
  // Offsets and widths, each from the instruction that encodes it. The position pair's
  // offsets come from order_select_larva, which loads CUnit::pSprite and builds a 16-bit
  // map rectangle around +0x14 and +0x16: a word load feeding a 16-bit add, so two bytes.

  //   +0x00  pPrev / +0x04 pNext   the CLink node pair. pNext is exercised:
  //                    CalcAllDrawRects walks a whole y-axis row with
  //                    `mov edi, dword ptr [edi+4h]` from sgSpritesYAxisHead[i]. pPrev is
  //                    inferred, not exercised -- no body here reads it -- and named from
  //                    the node contract CLink::Unlink states by clearing both dwords. No
  //                    base class is derived from the pair; src117/h/CLink.h says why the
  //                    cast stays at the call site.

  //   +0x0A  bCreator   written and compared byte to byte by CSprite::SetCreator and
  //                    CSprite::Init, so one byte. Nothing widens it, so its sign is
  //                    unfalsified rather than proven.

  //   +0x0B  bSelectionIndex  stored as one byte by CSprite::SelectAdd, on the arm that
  //                    has just set the selected flag. Sign undiscriminated, as
  //                    bCreator's; the name is from the runtime-verified 1.16.1 layout.

  //   +0x0D  bElevationLevel  `movzx eax, byte ptr [edx+0Dh]` / `cmp eax, 4h` with `jnbe`
  //                    in CSprite::GetSpritePriority: one byte, unsigned, and here the
  //                    sign is discriminated.

  //   +0x10  wIndex     `movzx ecx, word ptr [edx+10h]` in GetSpritePriority, so two bytes
  //                    unsigned. It is the pool index: InitSprites writes it 1, 2, 3 ...
  //                    while striding the pool, and writes element 0's slot with zero.

  //   +0x18  pMainImage  a dword immediately dereferenced at +8 as a CImage::wImageID.
  //   +0x1C  pImageHead  opens every forward image walk in the TU, each advancing through
  //                    CImage+0x04.
  //   +0x20  pImageTail  a dword dereferenced as a CImage, and specifically the tail of
  //                    the +0x1C list by CSprite::AllocImageBelow, which takes the address
  //                    of both and hands them to CLink::AppendLink as a pair.
  //                    CSprite::RemoveTeamCircle confirms the direction independently: it
  //                    starts at +0x20 and advances through CImage+0x00.

  // The position pair's signedness was settled by measurement, and the two halves came
  // out differently. wPosY is signed: five sites in this TU sign-extend it, the two that
  // zero-extend it are sign-neutral, and declaring it signed needs no casts where WORD
  // needs an explicit `(short)` at each of the five. Both spellings compile to 1.17's
  // bytes, so this is a judgement about the source a person would write, not a proof.

  // wPosX stays unsigned, and that was tested: declaring it signed regresses
  // CBullet::DispatchBounce, a landed body that reads the same field unsigned and is not
  // sign-dead about it. The sites that do read it signed -- FindClosestMergeTarget,
  // CUnit::IsOffScreen, CImage::DrawUpdateRect -- carry a `(short)` cast at the point of
  // use, and those casts are what keep those bodies matching: do not clean them up.
  // Note that `movzx eax, word ptr [m]` plus `movsx r32, ax` is not `(short)field`, which
  // folds to one movsx; it is an int intermediate and then the cast.

  // Everything else is unmapped: real fields we have not needed yet, held at true size so
  // the named ones keep their offsets.
  /*0x00*/ CSprite *pPrev;   // inferred from the node contract, not exercised
  /*0x04*/ CSprite *pNext;
  //   +0x08  wSpriteID  WORD stores in CSprite::Init and CSprite::ReplaceWith and a
  //                    zero-extending read in CSprite::Duplicate: two bytes, unsigned.
  /*0x08*/ WORD   wSpriteID;
  /*0x0A*/ BYTE   bCreator;
  /*0x0B*/ BYTE   bSelectionIndex;
  //   +0x0C  bVisibilityFlags  one bit per player, and that is what fixes the meaning
  //                    rather than just the width: sub_433DF0 bit-tests
  //                    `1 << pSource->bPlayer` against this byte, and two sites in
  //                    src117/lang/usel.cpp and it against gdwReplayVisionMask and
  //                    gdwLocalMask, which are player sets. One byte at every site, used
  //                    only bitwise and never compared as a number. Spelled `Flags`
  //                    because every use is a mask test.
  /*0x0C*/ BYTE   bVisibilityFlags;
  /*0x0D*/ BYTE   bElevationLevel;
  /*0x0E*/ sprite_flags_t flags;
  //   +0x0F  bFlashCounter  a countdown: CSprite::UpdateFlash loads it, decrements,
  //                    stores it back and tests the decrement against zero.
  /*0x0F*/ BYTE   bFlashCounter;
  /*0x10*/ sprite_index_t wIndex;  // Hyperion: a WORD in 1.17
  //   +0x12/+0x13  the cached GRP draw extents. CSprite::Init's tail reads the main
  //                    image's GROUP width and height, clamps each to 255 and stores them
  //                    one byte each. The consumer side agrees: usel.cpp's unit-pick loop
  //                    multiplies the pair as a clickable area.
  //                    Hyperion: read through SpriteGrpWidth and SpriteGrpHeight, below,
  //                    which keep a size past 255 by Hyperion's rules.
  /*0x12*/ BYTE   bGrpWidth;
  /*0x13*/ BYTE   bGrpHeight;
  // Hyperion: 4 bytes each on x64, wrapped to 16 as 1.17's (h/pos.h, stage E)
  /*0x14*/ upos_t wPosX;   // unsigned, and that was tested -- see above
  /*0x16*/ spos_t wPosY;   // signed -- see above; a judgement, not a byte-level proof
  /*0x18*/ CImage *pMainImage;
  /*0x1C*/ CImage *pImageHead;
  /*0x20*/ CImage *pImageTail;

  // The member functions, in oracle address order, which is the order
  // src117/lang/CSprite.cpp defines them in. Every one is __thiscall except the statics:
  // `this` arrives in ECX and every declared argument is on the stack, and /Gr does not
  // change that for a non-static member. Argument counts are read off each body's own
  // `ret N` -- `ret 4h` is one dword slot, a bare `ret` is none -- and the narrow types
  // off how the body consumes the slot.

  // oracle 0x415190 (87 B). Allocate this sprite's team circle, at most once per
  // team-circle slot. Nothing sets EAX on any path, so void, and the parameter is consumed
  // as one byte even though `ret 4h` cleans a full dword slot.
  void AllocCircle(BYTE bSelIndex);

  // oracle 0x4151F0 (102 B). Allocate a CImage and link it above this sprite's main
  // image. The head and tail arms and the return are AllocImageAbove's below, which this
  // shares; the one difference is that this body tests the head without naming it and
  // prepends onto pMainImage. x and y are undiscriminated here, and `char` is chosen
  // because it agrees with CImage::Initialize's own derived parameters rather than merely
  // compiling the same.
  CImage *AllocImage(int nImageID, char x, char y, int nDirection);

  // oracle 0x415260. Allocate a CImage and link it above this sprite's current head.
  // `ret 10h` cleans four dword slots and the body reads exactly four: three forwarded
  // into CImage::Initialize and the fourth into CImage::FaceDirection, whose own `int` is
  // what makes the fourth parameter int and not BYTE -- a BYTE needs a re-widening movzx,
  // and those four bytes are the difference. x and y are undiscriminated, Initialize's own
  // third and fourth parameters being `char` and an int argument to a char parameter still
  // forwarding the caller's slot unchanged. CUnit::CreateOverlay tail-jumps in here, so
  // the two signatures have to stay identical -- changing one lost the tail call and cost
  // 27 bytes. The return is a CImage *: the body treats CImage::AllocImage's result as the
  // `this` of three CImage members and returns that same pointer.

  // `__declspec(noinline)` is a steering lever, not a derivation. All three references in
  // the binary are calls, and without it /Ob2 splices this body into
  // CSprite::DamageOverlaysDown and DamageOverlaysUp. It perturbs nothing of this body's
  // own 100 bytes, and it has to stay on the declaration, which is all a generated TU
  // sees.
  __declspec(noinline) CImage *AllocImageAbove(int imgNo, char x, char y,
                                               int nDirection);

  // oracle 0x4152D0 (101 B). The mirror of AllocImage: the non-empty arm takes
  // CLink::AppendLink on pMainImage with the list's tail, where AllocImage takes
  // CLink::PrependLink with its head. Same four parameters, same derivations.
  CImage *AllocImageBelow(int nImageID, char x, char y, int nDirection);

  // oracle 0x415340 (38 B). Nothing sets EAX on the early-out path, so void.
  void AllocSelect(BYTE bSelIndex);

  // oracle 0x415400 (157 B). `ret 10h` => four dword slots. Pool-allocates through
  // CImage::AllocImage, links it in (PrependLink onto the head, or PrependThis against
  // head and tail when the list is empty), then forwards three slots into
  // CImage::Initialize and the fourth into CImage::FaceDirection, so the widths are
  // theirs.
  CImage *AllocSingleImage(int nImageID, char x, char y, int nDirection);

  // oracle 0x4154A0 (150 B). Declared only, an unported callee that resolves through the
  // generated thunk. CSprite::Duplicate's call site is the derivation: ECX takes wSpriteID
  // widened to a dword register, so int rather than WORD, EDX the sign-extended wPosX, and
  // the two stack slots the sign-extended wPosY and bCreator pushed as a dword. Static:
  // ECX carries the first parameter under /Gr.
  static
  CSprite *Create(int nSpriteID, int x, int y, BYTE bPlayer);

  // oracle 0x415590 (360 B). `ret 8h` => two dword slots, forwarded to
  // CImage::Initialize's nImageID and CImage::SetFacing's nDirection, so the widths are
  // theirs. Kills the whole image list, allocates a fresh main image, then re-creates the
  // healthbar and selection overlays the old list carried.
  void ChangeImage(int nImageID, int nDirection);

  // oracle 0x415370 (130 B). AllocImage's shape with the image id fixed at 0x1A8 and the
  // x/y pair fetched from the main image's GRP shield-offset table instead of arriving as
  // parameters. `ret 4h` => one dword slot, read whole. It returns void: the failure
  // branch lands on `pop edi`, so the success path's tail carries no `mov eax, esi`.
  void AllocShield(int nDirection);

  // oracle 0x415540 (73 B). Move another sprite's main image onto this sprite.
  // `ret 4h` => one dword slot, dereferenced at +0x18/+0x1C/+0x20 as a CSprite.
  void ClaimOwnership(CSprite *pSprite);

  // oracle 0x415750 (24 B).
  void UpdateDrawInfo();

  // oracle 0x415770 (47 B).
  void Cloak();

  // oracle 0x4157A0 (187 B). Bare `ret`, ECX only, so a nullary member. Walks pImageHead
  // twice hunting overlay ids in [0x1D8,0x1ED] then [0x1C2,0x1D7], and kills and re-allocs
  // one step down.
  void DamageOverlaysDown();

  // oracle 0x415860 (294 B). Bare `ret`, ECX only, so a nullary member. The mirror of
  // DamageOverlaysDown, with a net_rand(8) roll.
  void DamageOverlaysUp();

  // oracle 0x415990 (107 B). Two separate walks of the same list.
  void Decloak();

  // oracle 0x415A00 (120 B). Returns the removed circle's colouring byte, or 0, one byte
  // wide. The `__declspec(noinline)` keeps /Ob2 from splicing this body into
  // RemoveSelectsForSave once both are in one TU; every reference in the binary is a real
  // call. It is on the declaration so the generated TUs see it, and this body's own bytes
  // are unchanged either way.
  __declspec(noinline) BYTE Deselect();

  // oracle 0x415A80 (171 B). Bare `ret`, ECX only. Prepares the cloak palette rows for
  // this sprite's creator, then renders the image list tail to head -- `mov esi, [esi+20h]`
  // and then advancing through pPrev -- through each image's pfnRender.
  void Draw();

  // oracle 0x415B30 (204 B). Static nullary: iterates the priority list and draws each
  // sprite. The body is CSprite::Draw inlined by /Ob2, so DrawAll depends on Draw's
  // definition being in that same TU.
  static
  void DrawAll();

  // oracle 0x415C00 (224 B). ECX only, bare `ret`, and it returns the new sprite in EAX:
  // `mov eax, ebx` on the success tail, `xor eax, eax` after the Kill on the failure one.
  CSprite *Duplicate();

  // oracle 0x415CE0 (60 B). Bare `ret` => no arguments.
  void DoLandingDust();

  // oracle 0x415D20 (60 B). DoLandingDust with a different overlay table and a
  // different first SpawnDust argument.
  void DoLiftoffDust();

  // oracle 0x415E50 (169 B). `ret 0Ch` => three dword slots, matching CSprite::Hide's
  // `push 0h / push 0h / push 3h`.
  //
  // a2 and a3 are BYTE, and the callee is the authority: the body hoists both loop
  // invariants into the halves of one register (`mov bl, byte ptr [ebp+10h]` /
  // `mov bh, byte ptr [ebp+0Ch]`), one-byte loads an int parameter cannot produce, since
  // it loads the dword and destroys BH. Four body shapes under `int` were measured and all
  // miss, so the width lives in the declaration. The store side corroborates: a2 and a3
  // land in the one-byte coloringData.rle pair. a1 stays `int` deliberately -- the bytes
  // do not decide it, and uniformity would be an argument from symmetry.
  void sub_415E50(BYTE a1, BYTE a2, BYTE a3);

  // oracle 0x415F00 (37 B).
  void FaceDirection(int nDirection);

  // oracle 0x415F30 (92 B). Invalidate every image in the list; CImage::
  // Invalidate is inlined into it. Bare `ret` => no arguments.
  void ForceInvalid();

  // oracle 0x415F90 (144 B). `ret 4h` => one slot, and the width is the callee's first
  // instruction: `mov ax, word ptr [ebp+8h]`, a 16-bit read stored straight to wSpriteID.
  // For a parameter the callee is the authority.
  void ReplaceWith(WORD wSpriteID);

  // oracle 0x416020 (10 B), whole body `mov ecx, 501408h / jmp gamedata_free_data`.
  // Static is derived from the call site: free_game calls it with nothing loaded into ECX,
  // so no `this` is passed.
  //
  // A static member, not a file-static free function, and that is the hookability
  // question: a static member has external linkage, so src117/gen/hook_table.cpp can take
  // its address. The line break after `static` is deliberate -- the signature generator
  // reads a single line and would restate `static` on a file-scope definition, which is
  // C2724. Do not join it onto one line.
  static
  void FreeSprites();

  // oracle 0x416030 (29 B). `ret 8h` => two dword stack slots, and the caller
  // (CUnit::SyncConstructionGraphic) pushes `lea eax,[ebp-4h]` then
  // `lea eax,[ebp-8h]` -- two pointers to separate 1-byte locals.
  void GetRLEInfo(BYTE *pbRleFrame, BYTE *pbRleWait);

  // oracle 0x416050 (14 B).
  BYTE GetDamageOverlayCount();

  // oracle 0x416060 (7 B), whole body `mov eax,[ecx+20h] / mov al,[eax+0Fh] / ret`. The
  // caller sign-extends the result, which is what makes the byte signed.
  char GetEffectOffset();

  // oracle 0x416070 (18 B). Static: ECX is the sprite index, tested for zero then scaled
  // to sgSprites + 36*(n-1) with the -36 folded into the base. 1-based ids, 0 to NULL; the
  // derivation rests on GetSpriteID being its exact inverse.
  static
  CSprite *GetSprite(int nSprite);

  // oracle 0x416090 (27 B). Static, the inverse of GetSprite: NULL to 0, otherwise
  // (p - sgSprites)/36 + 1. The division is unsigned -- `mul` by a magic number, no sign
  // fix-up -- which plain CSprite* subtraction cannot produce, so the byte difference is
  // cast unsigned before dividing.
  static
  DWORD GetSpriteID(CSprite *pSprite);

  // oracle 0x415D60 (120 B). Static, bare `ret`, no argument reads: walks all 2500 pool
  // slots rewriting the three CImage* fields and the prev/next links in place as their
  // 1-based pool ids, ahead of the raw block write in SaveSprites.
  static
  void EncodeListForSave();

  // oracle 0x415DE0 (98 B). Static, the exact inverse of EncodeListForSave.
  static
  void DecodeListFromSave();

  // oracle 0x416790 (430 B). Static; ECX is the save-file handle, forwarded untouched to
  // pfread, so `void *` is a placeholder spelling and not a finding. Returns BOOL: every
  // failure path is `xor eax, eax` and success is `mov eax, 1h`.
  static
  BOOL LoadSprites(void *pFile);

  // oracle 0x416ED0 (566 B). Static; ECX is the save-file handle, the same placeholder
  // spelling. Returns BOOL, the pfwrite results carried through ESI and EDX.
  static
  BOOL SaveSprites(void *pFile);

  // oracle 0x416DA0 (123 B). Static, bare `ret`: walks all 2500 sprites' flag bytes,
  // Deselect()s the bit-3 holders, RemoveCircle()s the bit-0 holders, then clears bits
  // 1..2 and strips the [0x23B, 0x244] selection-circle images off the tail walk.
  static
  void RemoveSelectsForSave();

  // oracle 0x416E20 (163 B). Static, bare `ret`: re-creates the selection state after a
  // load, then the team-circle rebuild when IsTeamGame().
  static
  void RestoreSelectsFromSave();

  // oracle 0x4160B0 (46 B). The draw-order sort key.
  sprite_key_t GetSpritePriority();  // Hyperion: DWORD in 1.17 (stage D)

  // oracle 0x4160E0 (129 B). The bounding box of every visible image, written
  // through the caller's rect. `ret 4h` => one dword slot, used as an
  // out-pointer.
  void GetSpriteRect(rect32_t *prc);

  // oracle 0x4161C0 (65 B). Bare `ret` => no arguments.
  void Hide();

  // oracle 0x416170 (74 B). `ret 8h` => two dword slots, each a pointer receiving an
  // 8-byte {x, y} copied out of CImage::GetOverlayOffset on the main image: LO_SPECIAL (3),
  // overlays 0 and 1, the twin larva and addon spawn points.
  void GetTwinSpawnOffsets(shield_offset_t *pOffs0, shield_offset_t *pOffs1);

  // oracle 0x417280 (167 B), a callee only, with no body of ours. `ret 4h` => `this` in
  // ECX plus one dword stack slot, callee-cleaned.
  //
  // The parameter is a BYTE on the callee's authority -- for a parameter the callee is the
  // authority, for a return the callers are. The body reads that slot only as a byte:
  // `mov cl, byte ptr [ebp+8h] / movzx esi, cl / and esi, dword ptr [672668h]`, and again
  // before the store into bVisibilityFlags, where an `int` parameter would `and` the full
  // dword directly. Both of CUnit::Hide's call sites are identical at either width.
  //
  // The name is the ledger's. re/oracle117_map.json carries `SetVisibility` for this
  // address, the two sources disagree here and on 1,057 others, and the ledger is the one
  // the generator and the symbol check key on.
  void SetMask(BYTE bVisibility);

  // oracle 0x416210 (116 B). Hide every image that is not flagged draw-while-cloaked.
  // Bare `ret`, and CSprite::Cloak reaches it with `this` still live in ECX.
  // CImage::Hide's two statements are written out in the body rather than called, and
  // CImage::Invalidate is inlined inside them.
  void HideCloaked();

  // oracle 0x416290 (85 B). ECX only, bare `ret`, so a nullary member. Two head-to-tail
  // hunts of the image list (ids [0x188,0x194] then [0x3BE,0x3BF]); the hit tail-jumps
  // CImage::Hide, and a tail jmp pins the return type to void.
  void HidePowerup();

  // oracle 0x4162F0 (176 B). `ret 10h` => four slots, the widths from the callee's own
  // reads: the first is read whole into EDI and indexes two tables at register width, so
  // int; the next two are 16-bit reads compared unsigned against the map-size words, so
  // WORD; the fourth is a byte read. __thiscall, opening `mov esi, ecx`. Returns BOOL:
  // `xor eax, eax` on the bounds reject, `mov eax, 1h` on the success tail.
  //
  // An early version of this declaration was inserted into InitHash's static line-break
  // gap below, silently making Init static and InitHash __thiscall. The byte gate cannot
  // see that class of error, so the line break carries its own warning.
  BOOL Init(int nSpriteID, upos_t x, upos_t y, BYTE bPlayer);  // Hyperion: positions

  // oracle 0x4163A0 (100 B). Reset the visibility hash and rebuild its per-sprite-type
  // enable table. Static, derived: nothing loads ECX at the call and the body never reads
  // it. The line break after `static` is the FreeSprites one above -- do not join it.
  static
  void InitHash();

  // oracle 0x416410 (169 B). Static nullary: loads arr\sprites.dat, memsets the pool and
  // both y-axis tables, and free-links all 0x9C3 pool entries with ascending wIndex.
  static
  void InitSprites();

  // oracle 0x416520 (166 B). ECX only, bare `ret`. One walk of pImageHead: a dirty image
  // gets invalidate_rect, DrawUpdateRect and invalidate_rect again -- old rect, update,
  // new rect -- and a clean one gets DrawUpdateRect alone.
  void InvalidateDrawInfo();

  // oracle 0x4165D0 (241 B). Static: ECX and EDX are an inclusive y-axis-row range,
  // compared unsigned, so DWORD. InvalidateDrawInfo's per-image body over every sprite in
  // every row of the range.
  static
  void InvalidateSprites(DWORD yTop, DWORD yBottom);

  // oracle 0x4166D0 (66 B). `ret 8h` => two dword slots, forwarded whole into
  // CImage::ClickableAt. The two exits return a full dword, so `int` and not a byte.
  int IsClickablePt(int x, int y);

  // oracle 0x416720 (109 B). Kill every image, then move this sprite off its
  // y-axis row and onto the free list. Bare `ret` => no arguments.
  void Kill();

  // oracle 0x416940 (43 B). First image whose id falls in [first, last], else NULL. Both
  // bounds are compared unsigned against 32-bit registers, so the parameters are 32-bit
  // unsigned.
  CImage *OverlayGetImage(DWORD nFirstImageID, DWORD nLastImageID);

  // oracle 0x416970 (15 B).
  void sub_416970();

  // oracle 0x416A00 (176 B). Static: ECX and EDX are an inclusive y-axis-row range,
  // unsigned, so DWORD. Two walk variants selected by a mode global; it ends by passing
  // yTop on to SightHashUpdate when the vis-hash flag is armed.
  static
  void PriorityAddRange(DWORD yTop, DWORD yBottom);

  // oracle 0x416980 (60 B). `ret 8h` => two dword slots, compared signed against the four
  // members of a rect32_t built on the stack, which is what makes both the parameters and
  // rect32_t's members signed. The two exits are `xor eax, eax` and `mov eax, 1h`.
  int IsUnderPoint(int x, int y);

  // oracle 0x4169C0 (54 B). Returns the found image's colouring byte, or 0
  // (`xor al,al`) when there is none => one byte wide.
  BYTE RemoveTeamCircle();

  // oracle 0x416AB0 (35 B). Pixel Y to tile row, clamped to the map.
  int GetYTile();

  // oracle 0x416BB0 (26 B).
  void RedrawHP();

  // oracle 0x43A540 (61 B). `ret 4h`, and the `pop ebp / jmp CSprite::AllocSelect` tail
  // forwards the caller's one stack slot, which a nullary body cannot name. One pushed
  // BYTE, corroborated by CThingy::UpdateThingy2's `movzx eax, al / push eax / call`.
  void UpdateFlash(BYTE bColor);

  // oracle 0x4174D0 (103 B), unported. Nullary void; DispatchDeburrow's case-2 arm calls
  // it on this sprite.
  void ShowPowerup();

  // oracle 0x416CD0 (72 B). Clear the team-circle counter and remove the
  // circle, but only if the counter was non-zero. Bare `ret` => no arguments;
  // the result is RemoveTeamCircle's own one-byte colouring value, which this
  // body inlines rather than calls.
  BYTE RemoveTeamCircleIf();

  // oracle 0x416D20 (33 B).
  void ReleaseTeamCircle();

  // oracle 0x416D50 (70 B), an unported callee of CUnit::sub_432560, which reaches it as
  // `mov ecx, dword ptr [esi+0Ch] / call`: the sprite pointer straight into ECX, nothing
  // pushed and no stack adjustment after, so __thiscall with no arguments.
  //
  // It returns a BYTE, not void. All three exits set AL -- the removed circle's colouring
  // byte, RemoveTeamCircle's exact shape -- and the consuming caller, CSprite::ReplaceWith,
  // feeds AL into CImage::AllocSelect's bSelIndex. For a return the callers are the
  // authority, and only a caller that consumes it discriminates; the ones in
  // CUnitUpdate2 and CUnitSearches discard AL, so no landed body moved when this was
  // corrected.
  //
  // `__declspec(noinline)`: without it /Ob2 splices this body into the same-TU caller
  // RemoveSelectsForSave, where 1.17 emits a real call. On the declaration, not only the
  // definition, so the generated TUs see it too; the attribute does not change this body's
  // own bytes.
  __declspec(noinline) BYTE RemoveCircle();

  // oracle 0x40FE50. CUnit::RemovePlaceboxSelf loads its pSprite into ECX and
  // tail-jumps here with no stack arguments; the caller observes no result.
  void KillPlacebox();

  // oracle 0x417110 (67 B). `ret 8h` => two dword slots, each consumed as one byte: one
  // straight into bSelectionIndex, the other into CImage::AllocSelect's register parameter
  // through the inlined copy of CSprite::AllocSelect. A wider declaration on either would
  // emit a dword load.
  void SelectAdd(BYTE bSelectionIndex, BYTE bSelIndex);

  // oracle 0x417160 (37 B). The argument is read as a BYTE and compared
  // byte-to-byte against bCreator, so it is BYTE-typed even though `ret 4h`
  // cleans a full dword slot.
  void SetCreator(BYTE bPlayer);

  // oracle 0x417190 (37 B).
  void SetFacing(int nDirection);

  // oracle 0x417330 (195 B). Declared only; it resolves through the generated oracle
  // thunk. __thiscall with two dword stack slots (`ret 8h`), void return.
  //
  // The parameters are WORD, on the callee's authority: its entry reads are 16-bit
  // (`mov bx, word ptr [ebp+8h]` / `mov si, word ptr [ebp+0Ch]`), and an int parameter
  // cannot be steered into that -- four shapes were measured at 185 bytes against 1.17's
  // 195, where (WORD, WORD) reaches 195 in both forms. The callers push full dwords either
  // way, a WORD parameter still occupying a dword slot, so nothing in CFlingy.cpp moves.
  // The gate cannot see this disagreement, both spellings undecorating to one name, which
  // is why it is settled by the oracle's reads. The order is (x, y): the last push, hence
  // the first parameter, is the value both callers also store to CFlingy +0x28.
  void SetPosition(upos_t x, upos_t y);

  // oracle 0x4171C0 (55 B). The parameter is hoisted as a BYTE and stored
  // ZERO-extended (`movzx eax, bl`), which is what makes it unsigned.
  void SetShift(BYTE bShift);

  // oracle 0x417200 (29 B).
  void MakeDirty();

  // oracle 0x417220 (52 B). `ret 8h` => two dword slots.
  void SetAnimation(BYTE bAnim, int bForce);

  // oracle 0x417260 (22 B). `y` is `char` and forwarded untouched, which is
  // what earns the oracle's tail `jmp` -- see the body's comment.
  void SetShadowOffset(char y);

  // oracle 0x417400 (90 B). Two arms over the same image walk, selected by sprite flags
  // bit 6. Bare `ret`, so no arguments.
  void Show();

  // oracle 0x417460 (59 B). The inverse of HideCloaked -- un-hide every image that is not
  // flagged draw-while-cloaked, with CImage::Show's whole 21-byte read-modify-write
  // inlined. Bare `ret`, so no arguments.
  void ShowCloaked();

  // oracle 0x4174A0 (37 B).
  void ShowNormal();

  // oracle 0x417540 (156 B). Static: ECX is a y-axis row index and not a sprite -- it
  // seeds an unsigned clamp to 255 and walks a 256-byte table at 0x5AF138, never a
  // CSprite. PriorityAddRange passes its yTop here. Bare `ret`, nothing in EAX, so void.
  //
  // `__declspec(noinline)` must stay on the declaration: without it /Ob2 splices this
  // whole body into PriorityAddRange, where 1.17 emits a call, and the attribute on the
  // definition alone covers only the TUs that see the definition. Verified byte-identical
  // with and without it on this body itself, so it perturbs nothing of its own.
  static
  __declspec(noinline) void SightHashUpdate(DWORD nRow);

  // oracle 0x4176E0 (133 B). Declared only; it resolves through the generated oracle
  // thunk. __thiscall, no argument, void: the body ends on a bare `ret` and
  // CFlingy::StopAnim's call site pushes nothing and adjusts no stack.
  //
  // It maps the main image's animation through a 14-entry jump table into an idle
  // animation number and pushes that onto every image in the sprite -- whatever the thing
  // was doing, put it back to idle. That reading is why its one caller here is
  // CFlingy::StopAnim.
  void SwitchToIdle();

  // oracle 0x417620 (184 B). Declared only; it resolves through the generated oracle
  // thunk. `ret 14h` => five dword slots, exactly what CSprite::DoLandingDust and
  // DoLiftoffDust push, with `this` in ECX.
  //
  // Slots 2 and 3 are an overlay-index range and not an x/y pair: the callee seeds its
  // induction variable from the first, guards on the second, and pre-biases slot 4 once
  // with `sub dword ptr [ebp+14h], edi`, so the spawned thingy type is
  // nThingyType + (i - nFirst). DoLandingDust's (3, 0, 8, 0x143, 0) reads as a 9-overlay
  // LO range. Slot 5 is one byte by the callee's own `cmp byte ptr [ebp+18h], 0h`; the
  // other widths are undiscriminated, every argument at both call sites being a
  // `push imm`.
  void SpawnDust(int nLoType, int nFirstOverlay, int nLastOverlay,
                 int nThingyType, BYTE bAbove);

  // oracle 0x417840 (19 B). The inverse of sub_416970. Declared after ShowNormal because
  // 0x417840 follows 0x4174A0; the two free functions 1.17 places between them are below,
  // outside the class.
  void sub_417840();

  // oracle 0x417860 (104 B). Run every image's update procedure and its iscript, and
  // recycle the sprite when the last image removes itself. Bare `ret`, so no arguments. It
  // calls GetYTile rather than inlining it, which is MSVC's own size heuristic.
  void Update();
};

// Hyperion: bGrpWidth and bGrpHeight as fog, building sites and clicks read them. By
// Hyperion's rules a sprite clamped at 255 has its group's own size, so frames past 255
// count whole (revision 22, hxsim.h).
int SpriteGrpWidth(const CSprite *pSprite);
int SpriteGrpHeight(const CSprite *pSprite);

// Every offset asserted above, checked at compile time: these emit no code, and a later
// promotion out of an `_unmapped_` array that gets the arithmetic wrong stops the build
// here instead of silently moving fields.
OC_X86_LAYOUT(sizeof(CSprite)                    == 0x24, "CSprite size             (InitSprites `add esi, 24h` over 0x9C3 elements)");
static_assert(offsetof(CSprite, pPrev)           == 0x00, "CSprite::pPrev            [ecx]      -- the CLink node contract (INFERRED)");
OC_X86_LAYOUT(offsetof(CSprite, pNext)           == 0x04, "CSprite::pNext            [edi+4h]   -- CalcAllDrawRects's row walk");
OC_X86_LAYOUT(offsetof(CSprite, wSpriteID)       == 0x08, "CSprite::wSpriteID        [esi+8h]   -- word stores in Init/ReplaceWith, movzx in Duplicate");
OC_X86_LAYOUT(offsetof(CSprite, bCreator)        == 0x0A, "CSprite::bCreator         [ecx+0Ah]  -- SetCreator, Init");
OC_X86_LAYOUT(offsetof(CSprite, bSelectionIndex) == 0x0B, "CSprite::bSelectionIndex  [esi+0Bh]  -- SelectAdd");
OC_X86_LAYOUT(offsetof(CSprite, bVisibilityFlags)== 0x0C, "CSprite::bVisibilityFlags [ecx+0Ch]  -- SetLife, ANDed with the 0x672668 mask");
OC_X86_LAYOUT(offsetof(CSprite, bElevationLevel) == 0x0D, "CSprite::bElevationLevel  [edx+0Dh]  -- GetSpritePriority");
OC_X86_LAYOUT(offsetof(CSprite, flags)           == 0x0E, "CSprite::flags            [esi+0Eh]  -- AllocSelect, ReleaseTeamCircle");
OC_X86_LAYOUT(offsetof(CSprite, bFlashCounter)   == 0x0F, "CSprite::bFlashCounter    [esi+0Fh]  -- UpdateFlash countdown");
OC_X86_LAYOUT(offsetof(CSprite, wIndex)          == 0x10, "CSprite::wIndex           [edx+10h]  -- GetSpritePriority, InitSprites");
OC_X86_LAYOUT(offsetof(CSprite, bGrpWidth)       == 0x12, "CSprite::bGrpWidth        [esi+12h]  -- Init's clamped GROUP width store");
OC_X86_LAYOUT(offsetof(CSprite, bGrpHeight)      == 0x13, "CSprite::bGrpHeight       [esi+13h]  -- Init's clamped GROUP height store");
OC_X86_LAYOUT(offsetof(CSprite, wPosX)           == 0x14, "CSprite::wPosX            [eax+14h]");
OC_X86_LAYOUT(offsetof(CSprite, wPosY)           == 0x16, "CSprite::wPosY            [eax+16h]");
OC_X86_LAYOUT(offsetof(CSprite, pMainImage)      == 0x18, "CSprite::pMainImage       [ecx+18h]  -- GetDamageOverlayCount");
OC_X86_LAYOUT(offsetof(CSprite, pImageHead)      == 0x1C, "CSprite::pImageHead       [ecx+1Ch]  -- opens every forward walk");
OC_X86_LAYOUT(offsetof(CSprite, pImageTail)      == 0x20, "CSprite::pImageTail       [ecx+20h]  -- SetShadowOffset, RemoveTeamCircle");

// The free functions that belong to this TU. All are defined in src117/lang/CSprite.cpp,
// and the declarations are here so src117/gen/hook_table.cpp can take their addresses.
// They are in oracle address order, like the members above. None spells __fastcall: for a
// free function that is what /Gr already means.

// oracle 0x415700 (79 B). Invalidate every on-map image: for each y-axis row of the sprite
// grid, walk that row's sprite list and every sprite's image list. A free function and not
// a member: nothing loads ECX for it, the body never reads it, and the bare `ret` cleans no
// stack.
void CalcAllDrawRects();
// Hyperion: CalcAllDrawRects for the sprite rows yTop to yBottom only
void CalcDrawRectsInRows(DWORD yTop, DWORD yBottom);

// oracle 0x4175E0 (46 B). Snapshot the visibility hash into a caller record. `ret 4h`
// cleans exactly one stack slot and the entry shuffles ECX and EDX out of the way of
// `rep movsd`, so the first two parameters are the register pair; UpdateGameHash's three
// `lea`s confirm all three.
void SnapshotSightHash(BYTE *pbSeq, DWORD *pdwIndex, BYTE *pbBits);

// oracle 0x416BD0 (247 B). Stage the eight cloak remap rows for one player's colour into
// the block at dword_508890 + 0x800, which rle_draw_effect then indexes. Both arguments
// arrive in the BYTE halves of the register pair and nothing is pushed, so it is a free
// __fastcall taking two BYTEs.
//
// It is declared here rather than TU-locally because it is hooked: a thunk resolves from a
// .cpp prototype quite happily, but a hook row's declaration has to be on a header, since
// src117/gen/hook_table.cpp includes headers and nothing else.
void prepare_visible_cloak_draw(BYTE bPaletteType, BYTE bPlayer);

// oracle 0x416AE0 (194 B). Rebuilds the sprite free list after a load: scans sgSprites for
// the first slot with a null pImageHead, seeds sprites_sgpFreeHead and Tail with it, then
// CLink::AppendLink()s every later empty slot. Nullary void -- `xor ecx,ecx` is its first
// instruction, nothing is pushed, plain `ret`. A hooked row, so it is declared here rather
// than TU-locally.
void sub_416AE0();

// oracle 0x417790 (173 B). ECX is the image. Returns TRUE at the first dirty
// byte of the invalidation grid that the image covers.
BOOL IsInvalSet(CImage *pImage);

// oracle 0x417610 (11 B), whole body `mov dword ptr [0x5C59D0], 1h / ret`.
void EnableVisibilityHashUpdate();

// The sprite draw-priority family lives in src117/h/Priority.h, its bodies in
// src117/lang/Priority.cpp, 1.17's own TU for them: priority_add only emits that reload of
// sguiTop when GetSpritePriority is an extern MSVC cannot see through, which rules out
// CSprite.cpp as the home TU. A hook table includes `<TU-basename>.h`, so those
// declarations have to sit in a Priority.h we own, and only there.

// Declared-only callees with no owned header of their own yet. Each belongs somewhere
// else and is parked here because a src117/lang/CSprite.cpp body is its sole caller in
// this tree today and the thunk generator needs a declaration on the include path. Their
// real oracle TUs are named below; move each to that subsystem's own header the first time
// a second TU needs it. Both take their arguments in registers and clean no stack, which
// under /Gr is a free function's default __fastcall, so the keyword is not spelled.

// oracle 0x4443C0 (8 B), an empty stub in 1.17. Its real home is the gamedata subsystem;
// CSprite::FreeSprites is its sole caller here and passes its one argument in ECX. The
// parameter type is not derivable from a body that ignores its argument, so it is
// `void *`.
void gamedata_free_data(void *pGameData);

// oracle 0x4B2B10. Its real home is Teams.cpp. Reached with nothing in ECX or EDX and no
// push, so a free __fastcall with no arguments. The result is one byte:
// CSprite::AllocCircle, its only caller here, zero-extends it before use.
BYTE GetMaxPlayersPerTeam();
