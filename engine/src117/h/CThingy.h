// CThingy.h -- CThingy, the 16-byte doodad, effect and fogged-unit record, with the free
// functions of src117/lang/CThingy.cpp.
//
// The class is spelled here and nowhere else: a class defined in two headers is a C2011 as
// soon as both are included, and generated code resolves `CThingy::X` through the basename
// `CThingy.h` first. src117/h/CUnitZSpells.h includes this file for it.
#pragma once

#include "platform.h"
// point16_t is passed by value in sub_418000's signature, so a forward declaration will
// not do: a type a header's own prototype spells has to be in that header.
#include "point16.h"

class CSprite;
class CUnit;
class CThingy;

// The RecreateThingyList filter. RecreateThingyList calls it `mov ecx, [esi] / call edx`
// with a CSprite* in ECX and nothing else set up: one register argument, __fastcall.
typedef BOOL(__fastcall *thingy_sprite_query_t)(CSprite *pSprite);

// oracle 0x418820 / 0x418830 (8 B each): `xor eax,eax / test ecx,ecx / setz|setnz al /
// ret`. No explicit __fastcall on a free-function declaration, /Gr being the default.
BOOL QueryNullSprite(CSprite *pSprite);
BOOL QueryValidSprite(CSprite *pSprite);

// CThingy -- 16 bytes: GetDoodadThingy indexes with a stride of 16 off a 1-based id,
// GetDoodadThingyID is its inverse, and InitThingies memsets 500 * 16 bytes per pool and
// walks it `add esi, 10h`.
//
//   +0x00 pPrev / +0x04 pNext   the CLink node pair, as CSprite has it.
//   +0x08 nHitPoints  one slot with two readings, so it is a union: the doodad pool uses
//                     it as hit points, always 1, and the fogged-unit pool as the unit
//                     type the fogged sprite stands for. AllocFromSprite writes the low
//                     word, and UpdateThingy2 and sub_418520 read it back as a WORD to
//                     index gUnitPlaceboxSize.
//   +0x0C pSprite     the same offset as CUnit's and CBullet's pSprite: CThingy is the
//                     base shape all three share.
class CThingy {
public:
  /*0x00*/ CThingy *pPrev;
  /*0x04*/ CThingy *pNext;
  /*0x08*/ union {
    /*0x08*/ int  nHitPoints;
    /*0x08*/ WORD wUnitType;  // fogged pool: the unit type (index into arr/units)
  };
  /*0x0C*/ CSprite *pSprite;

  // ---- allocation ---------------------------------------------------------
  // oracle 0x417B80 (116 B), `ret 8h`. Static: there is no `this`, and ECX is the first
  // parameter under /Gr. Pops a node off the doodad free list, Creates its sprite and
  // prepends it to the used list -- or puts it back on the free list and returns NULL
  // when Create fails.
  static CThingy *AllocThingy(int nType, int x, int y, int nPlayer);

  // oracle 0x417B10 (104 B), bare `ret`. The fogged pool's allocator. ECX is read as CX
  // only (`mov di, cx`, stored as a WORD), which is what makes the first parameter WORD.
  static CThingy *AllocFromSprite(WORD wUnitType, CSprite *pSprite);

  // oracle 0x4183B0 (47 B), `ret 10h`. Returns `setnz` of the created sprite, so BOOL.
  BOOL Init(int nSpriteID, int x, int y, int nPlayer);

  // ---- per-frame update ---------------------------------------------------
  // oracle 0x417E50 (150 B). Recomputes the sprite's visibility byte from gpSquareMap at
  // the sprite's tile, SetMask()s it when it changed, and returns whether the sprite has
  // just gone hidden to the local player (gdwLocalMask).
  BOOL UpdateVisibility();
  // oracle 0x417C10 (259 B), nullary, `this` in ECX. Same job over the
  // sprite's whole GRP footprint (bGrpWidth/bGrpHeight) via sub_418D00.
  BOOL sub_417C10();
  // oracle 0x417D20 (300 B), `ret 8h`. The same job over a given width and height, both
  // pushed as dwords after a movzx, so int.
  BOOL CalcVisibility(int nWidth, int nHeight);
  // oracle 0x418B50 (136 B), nullary void, `this` in ECX (doodad pool).
  void UpdateThingy1();
  // oracle 0x418C20 (218 B), nullary void, `this` in ECX (fogged pool).
  void UpdateThingy2();
  // oracle 0x418520 (147 B), `ret 4h`. The flag picks sub_418E00 or sub_418EB0 through a
  // cmov into a function pointer and tail-calls it, so EAX is the callee's BOOL.
  BOOL sub_418520(int bFlag);
  // oracle 0x418B20 (33 B), nullary void.
  void SpriteUpdate();

  // ---- pool codecs (save/load) --------------------------------------------
  // oracle 0x418700 (129 B) / 0x418790 (129 B), `ret 8h`. The pool argument is three-way
  // and its third arm does nothing: NULL means the doodad pool, 0x5C7A30 the fogged pool,
  // and anything else no link work at all.
  void PostLoad(BOOL bLinked, CThingy *pPool);
  void PreSave(BOOL bLinked, CThingy *pPool);
  // 0x417C00, 5 B: a bare `jmp` to the shared empty body at 0x4BE950.
  void BitSwap();

  // oracle 0x418240 / 0x4182D0 (139 B each), static. Walk both 500-entry pools rewriting
  // pSprite (id to pointer through CSprite::GetSprite and GetSpriteID, real calls here)
  // and pPrev/pNext (1-based pool index, with the arithmetic inlined).
  static void DecodeListFromSave();
  static void EncodeListForSave();

  // oracle 0x418600 (242 B) / 0x418A10 (258 B), `ret 4h`. The file pointer is forwarded
  // untouched to pfread, pfwrite, compress and decompress, so `void *` is a placeholder
  // spelling, as in CSprite::LoadSprites.
  static BOOL LoadThingyList(void *pFile, CThingy *pPool, int nCount);
  // The two declarations genuinely diverge on the third parameter, and for a parameter the
  // callee is the authority: LoadThingyList reads its slot as a dword plus movzx, while
  // SaveThingyList reads it as a WORD three times with no init store, which an `int`
  // parameter never produces. Both call sites push the constant 500 as a dword either way,
  // so the difference costs them nothing. Hyperion: an int, for a pool past 65,535
  //.
  static BOOL SaveThingyList(void *pFile, CThingy *pPool, int nCount);
  // oracle 0x4185C0 (64 B) / 0x4189D0 (61 B), static, ECX = the file, bare
  // `ret`; both pools through the *ThingyList pair, then the codec. BOOL.
  static BOOL LoadThingies(void *pFile);
  static BOOL SaveThingies(void *pFile);

  // oracle 0x418840 (197 B), `ret 0Ch`. Rebuilds one list out of the pool entries whose
  // pSprite satisfies the query, in pool order, through CLink::AppendLink. Static void.
  static void RecreateThingyList(CThingy **ppHead, CThingy **ppTail,
                                 CThingy *pPool, int nCount,
                                 thingy_sprite_query_t pfnQuery);
  // oracle 0x418910 (121 B): the four calls above (free/used x two pools).
  static void RecreateThingyLists();

  // ---- lifecycle ----------------------------------------------------------
  // oracle 0x4183E0 (320 B), static void: memset both pools, seed the free
  // lists, allocate the black-X thingy (sprite 0x13E) into gBlackX.
  static void InitThingies();
  // oracle 0x418990 (56 B), static void: UpdateThingy1 over the doodad used
  // list, UpdateThingy2 over the fogged used list (next pointer taken before
  // the call -- the call may unlink the node).
  static void UpdateThingies();

  // ---- accessors ----------------------------------------------------------
  // oracle 0x418360 (6 B): `mov eax, [5C9978h] / ret`.
  static CThingy *FirstFoggedThingy();
  // oracle 0x418370 (17 B): 1-based id -> doodad-pool entry, 0 -> NULL.
  static CThingy *GetDoodadThingy(int nId);
  // oracle 0x418390 (20 B): the inverse; NULL -> 0.
  static int GetDoodadThingyID(CThingy *pThingy);

  // ---- the black X ---------------------------------------------------------
  // oracle 0x418BE0 (52 B), static void: gated on gbBlackXActive; tail-jumps
  // CSprite::MakeDirty.
  static void DrawBlackX();
};

// Every offset above, asserted: a comment is a claim, an assert is a proof the compiler
// re-runs on every TU that includes this header.
OC_X86_LAYOUT(sizeof(CThingy)                    == 0x10, "CThingy 16 B -- GetDoodadThingy `shl ecx,4`, GetDoodadThingyID `shr ecx,4`, InitThingies memsets 500*16=0x1F40 and walks `add esi,10h`");
static_assert(offsetof(CThingy, pPrev)           == 0x00, "CThingy::pPrev  -- CLink node pair head; AllocThingy hands `this` straight to CLink::Unlink");
OC_X86_LAYOUT(offsetof(CThingy, pNext)           == 0x04, "CThingy::pNext  -- RecreateThingyList zeroes [eax] and [eax+4] on a fresh node before AppendLink");
OC_X86_LAYOUT(offsetof(CThingy, nHitPoints)      == 0x08, "CThingy::nHitPoints -- `mov dword ptr [esi+8h], 1h` on every allocation path");
OC_X86_LAYOUT(offsetof(CThingy, wUnitType)       == 0x08, "CThingy::wUnitType  -- SAME SLOT: AllocFromSprite writes the low word `mov word ptr [esi+8h], di`, UpdateThingy2 and sub_418520 read it back `movzx eax, word ptr [edi+8h]` to index gUnitPlaceboxSize");
OC_X86_LAYOUT(offsetof(CThingy, pSprite)         == 0x0C, "CThingy::pSprite -- `mov dword ptr [esi+0Ch], eax` after CSprite::Create/Duplicate; same offset as CUnit and CBullet");

// The TU's free functions. They are declared here, not in CThingy.cpp, because generated
// code emits a hook row by including the body's `<TU>.h` and taking the function's
// address, so a hooked free function declared only in the .cpp has no declaration to
// find. Each carries an explicit __fastcall and is declared exactly once in the tree:
// two spellings of one name at one include root read as ambiguous.

// sub_418D00 / tiles_seen_mask -- oracle 0x418D00 (116 B) / 0x418D80 (115 B), both
// `ret 8h`. OR-accumulate the complement of a fog byte over a tile rectangle, rows
// top-down and columns right-to-left. They differ only in which byte of each fog dword
// they read: D00 takes byte 1, D80 byte 0 as CThingy::UpdateVisibility does. That
// asymmetry is 1.17's, reproduced deliberately -- do not "fix" it.
int  __fastcall sub_418D00(short x, short y, short w, short h);
int  __fastcall tiles_seen_mask(short x, short y, short w, short h);

// sub_418E00 / sub_418EB0 -- oracle 0x418E00 (170 B) / 0x418EB0 (161 B), both `ret 8h`,
// and mirrors of each other: E00 is TRUE on the first tile of the rectangle that is
// fogged or black (gdwLocalBlackMask), EB0 is FALSE on the first tile that is not visible
// (gdwLocalMask). CThingy::sub_418520 picks between them with a cmov into a function
// pointer and tail-calls it.
BOOL __fastcall sub_418E00(short x, short y, short w, short h);
BOOL __fastcall sub_418EB0(short x, short y, short w, short h);

// IsUnitRClickedCb -- oracle 0x417F00 (97 B). The find_first_unit_near filter behind
// GetRClickTarget. The second parameter is a WORD, and the callee is the authority: the
// body compares `cmp ax, dx`, where an `int` spelling emits `cmp eax, edx`.
BOOL __fastcall IsUnitRClickedCb(CUnit *pUnit, WORD wType);

// GetRClickTarget -- oracle 0x418210 (40 B), `ret 4h`. x and y are stored as WORDs to
// gnRClickX and gnRClickY, then the proximity search runs. The third parameter is an int
// narrowed at the push (`movzx eax,ax`); a WORD parameter would read the slot directly.
// The filter is cast at the call site, CBullet.h's pfn_bounce_filter_t taking one argument
// where ours takes the type in EDX as a second; both decorate to one symbol.
CUnit *__fastcall GetRClickTarget(int nX, int nY, int nType);

// sub_417F70 / sub_418000 -- oracle 0x417F70 (137 B) / 0x418000 (147 B, `ret 8h`). Both
// walk the fogged pool's used list for the first thingy whose sprite footprint contains a
// point, skipping unselectable unit types. They differ in where the footprint comes from:
// 417F70 reads gUnitPlaceboxSize and bounds with the sprite's GRP extents, 418000 filters
// by sprite id first and takes the footprint from a point16_t passed by value.
CThingy *__fastcall sub_417F70(int nX, int nY);
CThingy *__fastcall sub_418000(int nSpriteID, int nX, int nY, point16_t size);

// sub_4180A0 -- oracle 0x4180A0 (248 B), `ret 4h`. The same walk with the sprite id
// switched over 0xB2..0x119 and a tile-rect overlap test.
CThingy *__fastcall sub_4180A0(int nX, int nY, int nRadius);

// AllocBlackX (oracle 0x417AD0, 55 B) is declared in src117/h/minimap.h, its caller, as
// `void __fastcall AllocBlackX(int nX, int nY)`, and is not redeclared here. The body in
// CThingy.cpp lands against that declaration, so include minimap.h there.

// oracle 0x417EF0 (8 B), whole body: `mov byte ptr [5C9988h], 0h` / `ret`. Nothing is
// pushed, no register is read at the call site and EAX is never written, so `void ()` is
// the only signature consistent with the bytes.
//
// It is defined in src117/lang/CImage.cpp rather than in CThingy.cpp, which costs nothing
// (/Gy makes every function its own COMDAT), and the declaration is here so the hook table
// can take its address. 0x5C9988 (gbBlackXActive) is the only gate CThingy::DrawBlackX
// reads, so clearing it hides the black X, and its one caller is an arm of
// CImage::RunScript's iscript-opcode switch. Unnamed in every approved source, so the
// sub_ spelling stands; the first author proposed HideBlackX.
void sub_417EF0();
