// CSprite.cpp -- the sprite drawable: its image list (the six allocators, the walks over
// pImageHead, the show, hide, cloak and select clusters), the draw-order sort key, the
// render walk and its priority driver, the invalidate pair, the visibility hash, the pool's
// lifecycle and the save codecs. CImage::Invalidate is here too: 1.17 keeps it in this TU,
// which is what lets CSprite::ForceInvalid and CSprite::HideCloaked inline it.
//
// Bodies are in the classic game's own order, which is also src117/h/CSprite.h's
// declaration order.
//
// Two bodies depend on a sibling in this TU being inlined or not: CSprite::DrawAll is
// CSprite::Draw spliced in, so Draw's definition has to stay here, and
// CSprite::SightHashUpdate carries __declspec(noinline) to keep PriorityAddRange's call.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playervision.h"  // Hyperion: the slots past 7
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../port/data/port_formats.h"  // Hyperion
#include "../h/hxrules.h"  // Hyperion: HxRules, for SpriteGrpWidth
#include "../port/render/port_lights.h"  // Hyperion: PortLightImage, PortLightsBegin, PortLightsEnd
#include "../port/render/port_looks.h"  // Hyperion: a unit's look
#include "../port/render/port_scene.h"  // Hyperion: PortSceneImage
#include "../port/render/port_selection.h"  // Hyperion: PortSelectionImageShown
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/gameplay/port_eud.h"  // Hyperion: EudFrameMissing
#include "../gen/save117.h"
#include "CSprite.h"        // ours: the class, the three free functions, the parked callees
#include "Priority.h"       // ours: priority_init/add/iter_next -- bodies in Priority.cpp
#include "CImage.h"         // ours: CImage's layout and the CImage callees below
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "CLink.h"          // ours: the four intrusive-list entry points
#include "rect32.h"         // ours: the 32-bit screen rect three bodies below build
#include "debug.h"          // invalidate_rect
#include "grp.h"            // ours: GROUP/FRAME -- see the note below
#include "TrigAct.h"        // ours: int __fastcall net_rand(int nTag)
#include "CUnitZSpells.h"   // ours: class CThingy (AllocThingy/UpdateVisibility)
#include "net_cmds.h"       // ours: PlayerData (+0x0A nTeam) for RestoreSelectsFromSave
#include "Teams.h"          // IsTeamGame, reached by RestoreSelectsFromSave
#include "saveload.h"       // pfread, decompress
#include "Storm.h"
#include "../h/img.h"  // Hyperion: _img_list
#include "../h/maplimits.h"  // Hyperion: maps past 256 tiles
#include "../h/hxrules.h"  // Hyperion: HxLimits, the draw order's wide key
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../port/replays/port_snap.h"  // Hyperion: the trainer's snapshots hold it
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX, SELECTION_HUMANS

// grp.h is here because CSprite::Draw computes a frame pointer to hand to the image's own
// render procedure, and CSprite::Init reads GROUP::wMaxWidth and wMaxHeight to cache the
// clamped sprite extents.

// The two CRT entry points this TU reaches, declared by hand rather than through
// <string.h>. These spellings are the ones /Oi expands inline, which is what makes
// SnapshotSightHash's constant-size 256-byte copy a `rep movsd` and CSprite::InitHash's
// 256-byte clear a `rep stosd`.
extern "C" void *__cdecl memcpy(void *, const void *, size_t);
extern "C" void *__cdecl memset(void *, int, size_t);

// The globals this TU reaches, each spelled for either build: an `extern` where the address
// comes from the linker, a baked absolute where the payload needs one.

// The extents come from the .dat row counts -- 999 images, 517 sprites, 228 units, 209
// flingies -- and from the gap to the next named global; InitHash's own `cmp eax, 0E4h`
// corroborates the 228. sgSprites is 2500, since InitSprites memsets 90000 = 2500 * 36, and
// 1.17's y-axis row tables are 256. byte_529056 is 12 rows of 8: initPlayerColorMap loads
// 0x60 = 96 bytes into it and prepare_visible_cloak_draw indexes `[colour*8 + k]`.

// gdwLocalMask and gdwReplayVisionMask stay DWORD; PriorityAddRange's low-byte read is a
// cast at the use site. byte_6D0BF5's name is positional, no record naming that address,
// and only its width is derived -- eleven references, every one byte-wide.
#ifdef OC_DLL_MODE
#define gSPRITESArrayDataTbl_g  (reinterpret_cast<array_data_t*>(OC_VA(0x501408u)))
#define light_g                 (reinterpret_cast<light_t*>(OC_VA(0x50883Cu)))
#define guwFlingySprite         (reinterpret_cast<WORD *>(OC_VA(0x510AE0u)))
#define gubImageUseScript       (reinterpret_cast<BYTE *>(OC_VA(0x512EB8u)))
#define img_drawIfCloaked       (reinterpret_cast<BYTE *>(OC_VA(0x5132A0u)))
#define gubImageRLE             (reinterpret_cast<BYTE *>(OC_VA(0x513688u)))
#define img_dmgOverlay          (reinterpret_cast<DWORD *>(OC_VA(0x516D38u)))
#define img_landingDustOverlay  (reinterpret_cast<DWORD *>(OC_VA(0x518C78u)))
#define img_liftoffDustOverlay  (reinterpret_cast<DWORD *>(OC_VA(0x519C18u)))
#define guwSpriteImage          (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
#define sprite_visible          (reinterpret_cast<BYTE *>(OC_VA(0x51F248u)))
#define gubUnitFlingy           (reinterpret_cast<BYTE *>(OC_VA(0x51FAA8u)))
#define gulUnitTypeFlags        (reinterpret_cast<DWORD *>(OC_VA(0x521660u)))
#define gfEditFlags             (reinterpret_cast<WORD *>(OC_VA(0x524708u)))
#define gwMapHeight             (*reinterpret_cast<WORD *>(OC_VA(0x5264B6u)))
#define byte_529056             (reinterpret_cast<BYTE (*)[8]>(OC_VA(0x529056u)))
#define gwMapPixWidth           (*reinterpret_cast<WORD *>(OC_VA(0x543230u)))
#define gwMapPixHeight          (*reinterpret_cast<WORD *>(OC_VA(0x543234u)))
#define sgbNumDmgOverlays_g     (reinterpret_cast<BYTE *>(OC_VA(0x543310u)))
#define sgbVisHashTypeEnabled_g (reinterpret_cast<BYTE *>(OC_VA(0x5AEF30u)))
#define sgbPriorityVisHash_g    (reinterpret_cast<BYTE *>(OC_VA(0x5AF138u)))
#define sgSprites               (reinterpret_cast<CSprite *>(OC_VA(0x5AF238u)))
#define sprites_sgpFreeHead     (*reinterpret_cast<CSprite **>(OC_VA(0x5C51C8u)))
#define sprites_sgpFreeTail     (*reinterpret_cast<CSprite **>(OC_VA(0x5C51CCu)))
#define sgSpritesYAxisHead      (reinterpret_cast<CSprite **>(OC_VA(0x5C51D0u)))
#define sgSpritesYAxisTail      (reinterpret_cast<CSprite **>(OC_VA(0x5C55D0u)))
#define g_LocalHumanID          (*reinterpret_cast<DWORD *>(OC_VA(0x508D54u)))
#define gpNewUnits              (reinterpret_cast<CUnit **>(OC_VA(0x6A6070u)))
#define gSelected               (reinterpret_cast<CUnit **>(OC_VA(0x6A5EF0u)))
#define gPlayerData             (reinterpret_cast<PlayerData *>(OC_VA(0x6726C8u)))
#define sgbVisHashEnabled_g     (*reinterpret_cast<DWORD *>(OC_VA(0x5C59D0u)))
#define sgbVisHashSeq_g         (*reinterpret_cast<BYTE *>(OC_VA(0x5C59D4u)))
#define sgbVisHashBits_g        (reinterpret_cast<BYTE *>(OC_VA(0x5C59D8u)))
#define sgdwVisHashIndex_g      (*reinterpret_cast<DWORD *>(OC_VA(0x5C5AD8u)))
#define gdwLocalMask            (*reinterpret_cast<DWORD *>(OC_VA(0x672668u)))
#define gdwReplayVisionMask     (*reinterpret_cast<DWORD *>(OC_VA(0x67CC84u)))
#define sgbShowAll_g            (*reinterpret_cast<DWORD *>(OC_VA(0x67CC88u)))
#define gPlayback               (*reinterpret_cast<DWORD *>(OC_VA(0x67CC90u)))
#else
extern array_data_t    gSPRITESArrayDataTbl_g[7] OC_UNALIGNED;
extern light_t light_g[7];
extern WORD    guwFlingySprite[209] OC_UNALIGNED;
extern BYTE    gubImageUseScript[999];
extern BYTE    img_drawIfCloaked[999];
extern BYTE    gubImageRLE[999];
extern DWORD   img_dmgOverlay[999];
extern DWORD   img_landingDustOverlay[999];
extern DWORD   img_liftoffDustOverlay[999];
extern WORD    guwSpriteImage[517];
extern BYTE    sprite_visible[517] OC_UNALIGNED;
extern flingy_id_t gubUnitFlingy[228] OC_UNALIGNED;
extern DWORD   gulUnitTypeFlags[228] OC_UNALIGNED;
extern WORD    gfEditFlags[228] OC_UNALIGNED;
extern WORD    gwMapHeight;
extern BYTE    byte_529056[12][8] OC_UNALIGNED;
extern upos_t  gwMapPixWidth;
extern upos_t  gwMapPixHeight;
extern BYTE    sgbNumDmgOverlays_g[999] OC_UNALIGNED;
extern BYTE    sgbVisHashTypeEnabled_g[517];
// Hyperion: one entry per tile row of the largest map (h/maplimits.h),
// in storage of the port's own; 1.17's are [256] at 0x5AF138, 0x5C51D0 and 0x5C55D0.
BYTE    g_spritePriorityVisHash[MAP_SIDE_MAX];
CSprite *g_spriteRowHead[MAP_SIDE_MAX];
CSprite *g_spriteRowTail[MAP_SIDE_MAX];
PORT_SNAP_KEEP(g_spritePriorityVisHash);  // Hyperion: the trainer's snapshots hold them
PORT_SNAP_KEEP(g_spriteRowHead);
PORT_SNAP_KEEP(g_spriteRowTail);
extern CSprite sgSprites[POOL_SPRITES_STORE];
extern CSprite *sprites_sgpFreeHead;
extern CSprite *sprites_sgpFreeTail;
extern DWORD    g_LocalHumanID;
extern CUnit   *gpNewUnits[SELECTION_MAX] OC_UNALIGNED;
extern CUnit   *gSelected[SELECTION_HUMANS * SELECTION_MAX] OC_UNALIGNED;        // extent: select_init's memset of 0x180
extern PlayerTable<PlayerData, PlayerTagData> gPlayerData;
extern DWORD   sgbVisHashEnabled_g;
extern BYTE    sgbVisHashSeq_g;
extern BYTE    sgbVisHashBits_g[256] OC_UNALIGNED;
extern DWORD   sgdwVisHashIndex_g;
extern DWORD   gdwLocalMask;
extern DWORD   gdwReplayVisionMask;
extern DWORD   sgbShowAll_g;
extern DWORD   gPlayback;
#endif

// The callees no owned header declares yet, each spelled exactly once. Every one spells
// __fastcall explicitly, although /Gr already makes it the default.

// The DAT loader, reached by CSprite::InitSprites with the file name in ECX and
// gSPRITESArrayDataTbl_g in EDX -- two register arguments and nothing pushed. The return
// type is undiscriminated: this caller ignores EAX, so `void` is a placeholder.
void load_array_data(const char *pszFile, void *pArrayData);

// oracle 0x41D4F0 (71 B). One byte argument in CL and a one-byte result;
// prepare_visible_cloak_draw widens the result with `movzx eax, al`.
BYTE __fastcall get_unit_color_override(BYTE bPlayer);

// prepare_visible_cloak_draw (oracle 0x416BD0) is deliberately not declared here: it is
// defined below and is a hooked row, so its declaration belongs in src117/h/CSprite.h. A
// TU-local prototype is enough for a thunk and not for a hook.

// priority_init / priority_add / priority_iter_next are declared in src117/h/CSprite.h and
// defined in src117/lang/Priority.cpp, 1.17's own TU for that family.

// The render entry stored at CImage+0x34, declared void * in CImage.h. CSprite::Draw's call
// site is the derivation: ECX and EDX take the screen pair, and three stack slots take the
// FRAME pointer, the rect and the +0x30 colouring value, pushed right to left.
// Hyperion: the coloring value goes as its pointer-wide arm, which the render
// procedures take. The union itself went in a register on x86 and x64 alike, but
// WebAssembly passes a union by its address, and the procedures read the address as
// their table.
typedef void(__fastcall *IMAGERENDERPROC)(int x, int y, FRAME *pFrame,
                                          rect32_t *prc,
                                          DWORD_PTR dwColoring);

// Allocate this sprite's team circle, at
//           most once per team-circle slot.
//
//           The counter is flags bits 1..2. nTTL is a named local because 1.17 extracts it
//           before the call and re-reads the flags byte for the increment. The last slot,
//           GetMaxPlayersPerTeam() - 1, is never re-allocated.
void CSprite::AllocCircle(BYTE bSelIndex) {
  int nTTL = this->flags.bits.nTeamCircleTTL;
  if (nTTL == GetMaxPlayersPerTeam() - 1)
    return;

  this->flags.bits.nTeamCircleTTL++;
  if (this->flags.bits.nTeamCircleTTL > 1)
    return;

  if (!CImage::AllocSelect(this, bSelIndex, 0x23B))
    this->flags.bits.nTeamCircleTTL = 0;
}

// Allocate a CImage and link it above this
//           sprite's main image; the other Alloc* bodies are variations on it. The
//           non-empty arm's CLink call is what tells the first three apart:
//             AllocImage       pMainImage->PrependLink(new, &head)
//             AllocImageAbove  head->PrependLink(new, &head), the head named and loaded
//             AllocImageBelow  pMainImage->AppendLink(new, &tail)
CImage *CSprite::AllocImage(int nImageID, char x, char y, int nDirection) {
  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return 0;

  if (this->pImageHead) {
    reinterpret_cast<CLink *>(this->pMainImage)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pImageHead));
  } else {
    this->pMainImage = pImage;
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&this->pImageHead),
        reinterpret_cast<CLink **>(&this->pImageTail));
  }

  pImage->Initialize(nImageID, this, x, y);
  pImage->InitScript();
  pImage->FaceDirection(nDirection);
  return pImage;
}

// AllocImage's shape, prepending onto the
//           list head rather than onto pMainImage, and naming the head.
//
//           CUnit::CreateOverlay (src117/lang/CImage.cpp) tail-jumps into this function, so
//           the two declarations have to stay identical. The __declspec(noinline), repeated
//           from src117/h/CSprite.h, keeps it out of DamageOverlaysDown and DamageOverlaysUp,
//           which 1.17 reaches by a real call.
__declspec(noinline) CImage *CSprite::AllocImageAbove(int imgNo, char x, char y,
                                                      int nDirection) {
  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return 0;

  CImage *pHead = this->pImageHead;
  if (pHead) {
    reinterpret_cast<CLink *>(pHead)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pImageHead));
  } else {
    this->pMainImage = pImage;
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&this->pImageHead),
        reinterpret_cast<CLink **>(&this->pImageTail));
  }

  pImage->Initialize(imgNo, this, x, y);
  pImage->InitScript();
  pImage->FaceDirection(nDirection);
  return pImage;
}

// AllocImage with the non-empty arm taking
//           CLink::AppendLink on pMainImage with the list's tail, where AllocImage takes
//           CLink::PrependLink with its head. Everything else is AllocImage's.
CImage *CSprite::AllocImageBelow(int nImageID, char x, char y, int nDirection) {
  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return 0;

  if (this->pImageHead) {
    reinterpret_cast<CLink *>(this->pMainImage)->AppendLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pImageTail));
  } else {
    this->pMainImage = pImage;
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&this->pImageHead),
        reinterpret_cast<CLink **>(&this->pImageTail));
  }

  pImage->Initialize(nImageID, this, x, y);
  pImage->InitScript();
  pImage->FaceDirection(nDirection);
  return pImage;
}

// Allocate this sprite's selection circle,
//           once. `test byte ptr [esi+0Eh], 1h` is the "already has one" guard, and the
//           flag is only set once the allocation succeeds. ECX still holds `this` at the
//           call, so CImage::AllocSelect is declared static __fastcall rather
//           than __thiscall. Nothing sets EAX on the early-out path, so the return is void.
//           0x231 is the base image id of the selection-circle family.
void CSprite::AllocSelect(BYTE bSelIndex) {
  if (this->flags.b & 1)
    return;
  if (CImage::AllocSelect(this, bSelIndex, 0x231))
    this->flags.b |= 1;
}

// AllocImage's shape again -- the
//           PrependLink arm -- with the image id fixed at 0x1A8 and the x/y pair read out
//           of the main image's shield-offset table. It returns void.
//
//           The two byte copies before CImage::AllocImage stay in that order, since `off`'s
//           address escapes into GetShieldOffset; the locals are unsigned bytes.
void CSprite::AllocShield(int nDirection) {
  BYTE y, x;
  shield_offset_t off;

  this->pMainImage->GetShieldOffset(&off, nDirection);
  y = (BYTE)off.y;
  x = (BYTE)off.x;

  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return;

  if (this->pImageHead) {
    reinterpret_cast<CLink *>(this->pMainImage)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pImageHead));
  } else {
    this->pMainImage = pImage;
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&this->pImageHead),
        reinterpret_cast<CLink **>(&this->pImageTail));
  }

  pImage->Initialize(0x1A8, this, x, y);
  pImage->InitScript();
  pImage->FaceDirection(nDirection);
}

// CSprite::AllocImageAbove's shape --
//           allocate a pool CImage, link it in front of the list head -- followed by three
//           images.dat lookups the other Alloc* bodies do not do. The tables are indexed by
//           the image's own wImageID, not the parameter: gubImageRLE feeds CImage::SetRLE and
//           gubImageUseScript feeds flags bit 4, which image_flags_t does not name.
CImage *CSprite::AllocSingleImage(int nImageID, char x, char y, int nDirection) {
  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return 0;

  CImage *pHead = this->pImageHead;
  if (pHead) {
    reinterpret_cast<CLink *>(pHead)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pImageHead));
  } else {
    this->pMainImage = pImage;
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&this->pImageHead),
        reinterpret_cast<CLink **>(&this->pImageTail));
  }

  pImage->Initialize(nImageID, this, x, y);
  pImage->SetRLE(gubImageRLE[pImage->wImageID]);
  pImage->flags.w = (WORD)((gubImageUseScript[pImage->wImageID] & 1) << 4) |
                    (WORD)(pImage->flags.w & 0xFFEF);
  pImage->SetScriptDefault();
  pImage->FaceDirection(nDirection);
  return pImage;
}

// Pop a sprite off the free list,
//           Init it, and thread it onto its y-axis draw row. The tile arithmetic is written
//           out rather than calling GetYTile, which is defined further down; the two null
//           returns share one tail.
CSprite *CSprite::Create(int nSpriteID, int x, int y, BYTE bPlayer) {
  // Hyperion: a map's THG2 names any sprite; one the game lacks indexes past its tables
  if (nSpriteID < 0 || !SpriteTypeValid(static_cast<unsigned>(nSpriteID)))
    return 0;
  CSprite *pSprite = sprites_sgpFreeHead;
  if (pSprite == 0)
    return 0;
  reinterpret_cast<CLink *>(pSprite)->Unlink(
      reinterpret_cast<CLink **>(&sprites_sgpFreeHead),
      reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
  if (!pSprite->Init(nSpriteID, x, y, bPlayer)) {
    reinterpret_cast<CLink *>(pSprite)->PrependThis(
        reinterpret_cast<CLink **>(&sprites_sgpFreeHead),
        reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
    return 0;
  }

  int nTile = pSprite->wPosY / 32;
  if (nTile < 0)
    nTile = 0;
  else if (nTile >= gwMapHeight)
    nTile = gwMapHeight - 1;

  reinterpret_cast<CLink *>(pSprite)->PrependThis(
      reinterpret_cast<CLink **>(&g_spriteRowHead[nTile]),
      reinterpret_cast<CLink **>(&g_spriteRowTail[nTile]));
  return pSprite;
}

// Move another sprite's MAIN image onto this
//           sprite: unlink it from that sprite's list, prepend it to this one's, re-home
//           it, and clear one flag bit.
//
//           Two separate early-outs, not one `&&`: the two `jz`s land on different
//           instructions, because ESI is only saved once the first test passes. Bit 3 is
//           not named in image_flags_t, so the whole-word arm is used.
void CSprite::ClaimOwnership(CSprite *pSprite) {
  if (!pSprite)
    return;

  CImage *pImage = pSprite->pMainImage;
  if (!pImage)
    return;

  pSprite->pMainImage = 0;
  reinterpret_cast<CLink *>(pImage)->Unlink(
      reinterpret_cast<CLink **>(&pSprite->pImageHead),
      reinterpret_cast<CLink **>(&pSprite->pImageTail));

  reinterpret_cast<CLink *>(this->pImageHead)->PrependLink(
      reinterpret_cast<CLink *>(pImage),
      reinterpret_cast<CLink **>(&this->pImageHead));

  pImage->pParentSprite = this;
  pImage->flags.w &= 0xFFF7;
}

// Invalidate every on-map image: for each
//           y-axis row of the sprite grid, walk that row's sprite list and every sprite's
//           image list, calling CImage::DrawUpdateRect on each. The counter is unsigned.
//           gwMapHeight is re-read every iteration because DrawUpdateRect is opaque to this
//           TU and could change it; hoisting it into a local would drop the second load.
void CalcAllDrawRects() {
  for (DWORD y = 0; y < gwMapHeight; y++) {
    for (CSprite *pSprite = g_spriteRowHead[y]; pSprite; pSprite = pSprite->pNext) {
      for (CImage *pImage = pSprite->pImageHead; pImage; pImage = pImage->pNext)
        pImage->DrawUpdateRect();
    }
  }
}

// Hyperion: the same walk over the rows yTop to yBottom. With the GPU scene every drawn
// frame takes the map's full path (gamemap_update), and only the rows the priority list draws
// are read before the next frame's walk, as with 1.17's partial path: an image's rectangle is
// the view's, which the state check leaves out (port_synchash.cpp, Image), and a click is
// tested against rows in view. On a large map this was every image of the game each drawn
// frame (analysis-render.md, option 1).
void CalcDrawRectsInRows(DWORD yTop, DWORD yBottom) {
  for (DWORD y = yTop; y <= yBottom && y < gwMapHeight; y++) {
    for (CSprite *pSprite = g_spriteRowHead[y]; pSprite; pSprite = pSprite->pNext) {
      for (CImage *pImage = pSprite->pImageHead; pImage; pImage = pImage->pNext)
        pImage->DrawUpdateRect();
    }
  }
}

// The bare image walk, and the shape eleven
//           functions in this TU are variations on: load pImageHead, test,
//           body, advance through CImage::pNext, test, loop.
void CSprite::UpdateDrawInfo() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->DrawUpdateRect();
}

// Sibling call, then a three-term guarded
//           walk. No frame at all, which follows from the signature: a bare `ret` means no
//           arguments, and with no stack arguments and no locals /Ox has no frame to set up.
void CSprite::Cloak() {
  this->HideCloaked();
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->bPaletteType == 0 || pImage->bPaletteType == 4 ||
        pImage->bPaletteType == 7)
      pImage->CloakInit();
}

// Step the damage overlay one level DOWN:
//           find an image in the high band [0x1D8,0x1ED], kill it and re-alloc the same
//           slot in the low band [0x1C2,0x1D7]; with no high overlay, just kill the low one.
//           Both walks read `this->pImageHead` directly rather than through a named local,
//           which is what 1.17's two loop guards need, and both bounds are hoisted into
//           registers by the compiler.
void CSprite::DamageOverlaysDown() {
  CImage *pMain = this->pMainImage;
  if (img_dmgOverlay[pMain->wImageID] == 0)
    return;

  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->wImageID >= 0x1D8 && pImage->wImageID <= 0x1ED) {
      shield_offset_t off;
      int nOverlay = pImage->wImageID - 0x1D8;
      pMain->GetOverlayOffset(&off, 1, nOverlay);
      pImage->Kill();
      this->AllocImageAbove(nOverlay + 0x1C2, (char)off.x, (char)off.y, 0);
      return;
    }
  }
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->wImageID >= 0x1C2 && pImage->wImageID <= 0x1D7) {
      pImage->Kill();
      return;
    }
  }
}

// The mirror of DamageOverlaysDown: step an
//           existing low overlay [0x1C2,0x1D7] up into the high band, or -- when there is
//           none -- roll net_rand(8) over half the type's overlay count and walk the 22
//           LO_SPECIAL slots until that many are free. The table read is its own statement,
//           `o` comes before nCount, and the divide is unsigned, all as 1.17 has them. Its
//           divide by zero is kept: when sgbNumDmgOverlays_g[nMainId] is 0 or 1 the `div`
//           faults (docs/open-issues.md).
void CSprite::DamageOverlaysUp() {
  CImage *pMain = this->pMainImage;
  int nMainId = pMain->wImageID;
  if (img_dmgOverlay[nMainId] == 0)
    return;

  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->wImageID >= 0x1C2 && pImage->wImageID <= 0x1D7) {
      shield_offset_t off;
      int nOverlay = pImage->wImageID - 0x1C2;
      pMain->GetOverlayOffset(&off, 1, nOverlay);
      pImage->Kill();
      this->AllocImageAbove(nOverlay + 0x1D8, (char)off.x, (char)off.y, 0);
      return;
    }
  }

  shield_offset_t o = {0, 0};
  BYTE bNumOverlays = sgbNumDmgOverlays_g[nMainId];
  DWORD nCount = (DWORD)net_rand(8) % (bNumOverlays / 2) + 1;
  int nAdded = 0;
  int nSlot = -1;
  for (int j = 0;; j++) {
    if (j >= 0x16)
      j = 0;
    shield_offset_t off;
    o = *this->pMainImage->GetOverlayOffset(&off, 1, j);
    if (o.x != 0x7F || o.y != 0x7F) {
      DWORD nId = 0x1D8 + j;
      nAdded++;
      CImage *pImage;
      for (pImage = this->pImageHead; pImage; pImage = pImage->pNext)
        if (pImage->wImageID >= nId && pImage->wImageID <= nId)
          break;
      if (pImage == 0 && nAdded >= (int)nCount) {
        nSlot = j;
        break;
      }
    }
  }
  if (nSlot != -1)
    this->AllocImageAbove(nSlot + 0x1C2, (char)o.x, (char)o.y, 0);
}

// Two separate walks of the same list, the
//           second re-reading pImageHead. The first compares bPaletteType against 2, 3, 5
//           and 6 with the member read in each compare (a named BYTE local lets MSVC
//           range-reduce the pair). The second is CImage::Show written out, since it lives in
//           another TU.
void CSprite::Decloak() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->bPaletteType == 2 || pImage->bPaletteType == 3 ||
        pImage->bPaletteType == 5 || pImage->bPaletteType == 6)
      pImage->DecloakInit();
  }

  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (!img_drawIfCloaked[pImage->wImageID]) {
      if (pImage->flags.b.Hidden) {
        pImage->flags.b.Hidden = 0;
        pImage->flags.b.Dirty = 1;
      }
    }
  }
}

// Drop the selection: remove the health bar,
//           then the selection circle, returning the circle's colouring byte or 0.
//
//           The second walk is CSprite::RemoveCircle written out (it is defined later); only
//           the id bounds differ from RemoveTeamCircle's, 0x231..0x23A against 0x23B..0x244.
//           The two results meet in one variable through the `goto`, as in 1.17; a `return`
//           in the loop tail-merges the two epilogues instead.
__declspec(noinline) BYTE CSprite::Deselect() {  // noinline: see the header
  if (!(this->flags.b & 8))
    return 0;
  this->flags.b &= ~8;

  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->bPaletteType == 0x0B) {
      pImage->RemoveHPBar();
      break;
    }
  }

  BYTE bColoringData;
  if (this->flags.b & 1) {
    this->flags.b &= ~1;
    for (CImage *pImage = this->pImageTail; pImage; pImage = pImage->pPrev) {
      if (pImage->wImageID >= 0x231 && pImage->wImageID <= 0x23A) {
        bColoringData = pImage->coloringData.b;
        pImage->RemoveCircle();
        goto done;
      }
    }
  }
  bColoringData = 0;
done:
  return bColoringData;
}

// Render one sprite: prime the player
//           colour, stage the cloak remap rows if the main image needs them, then walk the
//           image list tail to head calling each image's own render procedure, and clear
//           every image's Dirty bit on the way out.

//           The clear is outside every guard: it is the loop's last statement and all four
//           skip paths land on it, so a hidden, degenerate or clean image still has bit 0
//           cleared.

//           The rect is not a rectangle here: `right` and `bottom` carry the extents while
//           `left` and `top` carry the GRP-space origin, a source rectangle for the blitter
//           rather than the l/t/r/b shape CImage::Invalidate builds.
// Hyperion: health bars over every sprite. 1.17 draws a health bar (palette type
// 0x0B, one of the 12 images CImage::AllocHealthbar hands out) with its sprite, so a
// sprite drawn after it, lower on the screen, covers it; Hyperion draws them last, while
// CSprite::DrawAll runs (g_bHpBarsLast), with the colours of the sprite each belongs to.
// Presentation only.
static bool g_bHpBarsLast;
static CImage *g_apHpBars[SELECTION_WIDE];  // a selection's, of 255 in Hyperion's (stage S)
static CSprite *g_apHpBarSprites[SELECTION_WIDE];
static int g_nHpBars;

// An image's frame through its own render procedure, as CSprite::Draw calls it.
static void PortDrawImageNow(CImage *pImage, short w, short h) {
  rect32_t rc;
  // Hyperion: not a frame past the image's group (grp.h)
  if (!GroupHasFrame(pImage->pGroup, pImage->wFrameIndex)) {
    EudFrameMissing(pImage->wImageID, pImage->wFrameIndex, GroupFrames(pImage->pGroup));
    return;
  }
  rc.left = pImage->wGrpX;
  rc.right = w;
  rc.top = pImage->wGrpY;
  rc.bottom = h;
  PortSceneImage(pImage->wImageID, pImage->wFrameIndex,  // Hyperion: the HD art
                 pImage->pGroup->wMaxWidth, pImage->pGroup->wMaxHeight);
  reinterpret_cast<IMAGERENDERPROC>(pImage->pfnRender)(
      pImage->wScreenX, pImage->wScreenY,
      &pImage->pGroup->frames[pImage->wFrameIndex], &rc,
      pImage->coloringData.dw);
  PortSceneImage(-1, 0, 0, 0);
}

void CSprite::Draw() {
  sub_41D740(this->bCreator);
  if (this->pMainImage != 0)
    prepare_visible_cloak_draw(this->pMainImage->bPaletteType, this->bCreator);
  const int nLook = PortLookOfSprite(this);  // Hyperion: its look, if its unit has one

  for (CImage *pImage = this->pImageTail; pImage; pImage = pImage->pPrev) {
    if (!(pImage->flags.w & 0x40)) {
      // Hyperion: the 2D lights (port_lights.cpp), from every image shown, drawn
      // again this pass or not.
      PortLightImage(pImage->wImageID, pImage->wMapX, pImage->wMapY);
      short h = pImage->wDrawHeight;
      if (h > 0) {
        short w = pImage->wDrawWidth;
        if (w > 0) {
          // Hyperion: a selection's mark only as the selection's looks show it
          // (port_selection.h)
          if (((pImage->flags.w & 1) || IsInvalSet(pImage)) &&
              PortSelectionImageShown(pImage->wImageID, pImage->bPaletteType, this->bCreator)) {
            // Hyperion: a health bar waits for the end of the pass (above)
            if (g_bHpBarsLast && pImage->bPaletteType == 0x0B && g_nHpBars < SELECTION_WIDE) {
              g_apHpBars[g_nHpBars] = pImage;
              g_apHpBarSprites[g_nHpBars++] = this;
            } else {
              // Hyperion: an image its look covers through the look's colours
              const bool bLooked = nLook != 0 && PortLookImageBegin(nLook, pImage->wImageID);
              PortDrawImageNow(pImage, w, h);
              if (bLooked)
                PortLookImageEnd();
            }
          }
        }
      }
    }
    pImage->flags.w &= 0xFFFE;
  }
  if (nLook != 0)
    PortLookSpriteDrawn(this, nLook);  // Hyperion: its look's plume over it
}

// Draw every sprite the priority list hands
//           back, in list order. The body is CSprite::Draw spliced in by /Ob2, so
//           Draw's definition above has to stay in this TU; 1.17 inlined it too.
void CSprite::DrawAll() {
  PortLightsBegin();  // Hyperion: the 2D lights (port_lights.cpp)
  PortLooksPassBegin();  // Hyperion: the looked units' sprites (port_looks.h)
  g_bHpBarsLast = true;  // Hyperion: the health bars after every sprite (above)
  g_nHpBars = 0;
  for (CSprite *pSprite = priority_iter_begin(); pSprite;
       pSprite = priority_iter_next())
    pSprite->Draw();
  g_bHpBarsLast = false;
  for (int i = 0; i < g_nHpBars; ++i) {
    sub_41D740(g_apHpBarSprites[i]->bCreator);
    PortDrawImageNow(g_apHpBars[i], g_apHpBars[i]->wDrawWidth, g_apHpBars[i]->wDrawHeight);
  }
  g_nHpBars = 0;
  PortLightsEnd();
}

// Clone this sprite: make a bare one at the
//           same place, throw away the images CSprite::Create gave it, and re-create this
//           one's list image for image, skipping palette types 0x0B (the health bar) and
//           0x0D. The source list is walked from pImageTail back, so prepending keeps its
//           order. The two failure exits share one `return` by `goto`, and nImageID and the
//           position go through named locals, which fix 1.17's evaluation order.
CSprite *CSprite::Duplicate() {
  BYTE bCreator = this->bCreator;

  spos_t x = this->wPosX;
  spos_t y = this->wPosY;

  CSprite *pClone = CSprite::Create(this->wSpriteID, x, y, bCreator);
  if (!pClone)
    goto fail;

  CImage *pImage;  // Hyperion: set apart from its declaration, which a goto jumps past
  pImage = pClone->pImageHead;
  while (pImage) {
    CImage *pNext = pImage->pNext;
    pImage->Kill();
    pImage = pNext;
  }
  pClone->pMainImage = 0;

  for (CImage *pSrc = this->pImageTail; pSrc; pSrc = pSrc->pPrev) {
    if (pSrc->bPaletteType == 0x0B || pSrc->bPaletteType == 0x0D)
      continue;

    int nImageID = pSrc->wImageID;
    CImage *pNew = pClone->AllocSingleImage(nImageID,
                                            (BYTE)pSrc->cHorzOffset,
                                            (BYTE)pSrc->cVertOffset,
                                            pSrc->bDirection);
    if (!pNew) {
      pClone->Kill();
      goto fail;
    }

    pNew->wFrameSet = pSrc->wFrameSet;
    pNew->wFrameIndex = pSrc->wFrameIndex;
    pNew->FlipImage((BYTE)((pSrc->flags.w >> 1) & 1));
    pNew->DrawUpdateRect();

    if (pSrc == this->pMainImage)
      pClone->pMainImage = pNew;
  }

  return pClone;

fail:
  return 0;
}

// Spawn the landing dust pair, if this
//           sprite's main image has a landing-dust overlay at all.
void CSprite::DoLandingDust() {
  if (img_landingDustOverlay[this->pMainImage->wImageID]) {
    this->SpawnDust(3, 0, 8, 0x143, 0);
    this->SpawnDust(3, 0x10, 0x18, 0x143, 1);
  }
}

// DoLandingDust verbatim, with the table
//           img_liftoffDustOverlay (0x519C18) in place of img_landingDustOverlay
//           and the overlay kind 4 in place of 3. The note above applies
//           unchanged.
void CSprite::DoLiftoffDust() {
  if (img_liftoffDustOverlay[this->pMainImage->wImageID]) {
    this->SpawnDust(4, 0, 8, 0x143, 0);
    this->SpawnDust(4, 0x10, 0x18, 0x143, 1);
  }
}

// Re-tint the whole image list one of three
//           ways, after hiding the cloaked images.
//
//           All three parameters are BYTE: 1.17 holds two of them in BL and BH. a2 is the
//           RLE frame and a3 the RLE wait, the coloringData.rle pair at +0x30 and +0x31.
//           The arms are in source order 1, 2, 3; MSVC emits them 3, 2, 1.
void CSprite::sub_415E50(BYTE a1, BYTE a2, BYTE a3) {
  this->HideCloaked();

  switch ((BYTE)a1) {
  case 1:
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
      if (pImage->bPaletteType == 0) {
        pImage->SetRLE(2);
        pImage->coloringData.rle.bRleFrame = a2;
        pImage->coloringData.rle.bRleWait = a3;
      }
    }
    break;

  case 2:
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
      if (pImage->bPaletteType == 0) {
        pImage->SetRLE(4);
        pImage->coloringData.rle.bRleFrame = a2;
        pImage->coloringData.rle.bRleWait = a3;
      }
    }
    break;

  case 3:
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
      if (pImage->bPaletteType == 0 || pImage->bPaletteType == 2 ||
          pImage->bPaletteType == 4)
        pImage->SetRLE(3);
    }
    break;
  }
}

// The walk forwarding one stack argument.
void CSprite::FaceDirection(int nDirection) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->FaceDirection(nDirection);
}

// Invalidate every image in the list.
//
//           CImage::Invalidate is inlined here: 1.17 has no call at that point, only the
//           rect build and `call invalidate_rect`. Its definition sits further down this
//           same file, and MSVC does reach forward for a member body in the same TU.
void CSprite::ForceInvalid() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->Invalidate();
}

// Swap this sprite's type id, then rebuild
//           whichever selection decoration it currently has, carrying that decoration's
//           colouring byte across. Each arm keeps its own guard and AllocSelect call, as
//           1.17 does, and every guard after a call re-reads the flags byte, since Deselect
//           clears bits 0 and 3. CSprite::RemoveCircle's byte return feeds AllocSelect here.
void CSprite::ReplaceWith(WORD wSpriteID) {
  this->wSpriteID = wSpriteID;

  if (this->flags.b & 8) {
    BYTE bColoringData = this->Deselect();
    if (!(this->flags.b & 8)) {
      this->flags.b |= 8;
      CImage::AllocHealthbar(this);
      if (!(this->flags.b & 1)) {
        if (CImage::AllocSelect(this, bColoringData, 0x231))
          this->flags.b |= 1;
      }
    }
  } else if (this->flags.b & 1) {
    BYTE bColoringData = this->RemoveCircle();
    if (!(this->flags.b & 1)) {
      if (CImage::AllocSelect(this, bColoringData, 0x231))
        this->flags.b |= 1;
    }
  }

  if ((this->flags.b & 6) && this->pImageTail) {
    BYTE bTeamColoringData = this->RemoveTeamCircle();
    if (!CImage::AllocSelect(this, bTeamColoringData, 0x23B))
      this->flags.b &= ~6;
  }
}

// Release sprites.dat's array-data table: a
//           tail jump into gamedata_free_data, a __fastcall taking its argument in ECX.
//           Static, from the call site (src117/h/CSprite.h).
void CSprite::FreeSprites() {
  gamedata_free_data(gSPRITESArrayDataTbl_g);
}

// Two out-parameters, read off the main
//           image's colouring cell. pMainImage is reloaded between the two stores, because
//           MSVC cannot prove the out-pointer does not alias the field.
void CSprite::GetRLEInfo(BYTE *pbRleFrame, BYTE *pbRleWait) {
  *pbRleFrame = this->pMainImage->coloringData.rle.bRleFrame;
  *pbRleWait  = this->pMainImage->coloringData.rle.bRleWait;
}

// A byte-array global indexed by the main
//           image's WORD id.
BYTE CSprite::GetDamageOverlayCount() {
  return sgbNumDmgOverlays_g[this->pMainImage->wImageID];
}

// The tail image's cVertOffset, signed because
//           the caller sign-extends it. No null check on pImageTail: 1.17 dereferences it
//           unconditionally.
char CSprite::GetEffectOffset() {
  return this->pImageTail->cVertOffset;
}

// The 1-based pool index -> pointer
//           accessor: index 0 is the null sprite and every other index names
//           sgSprites[n-1]. Static; CSprite::GetSpriteID below is its inverse.
CSprite *CSprite::GetSprite(int nSprite) {
  if (nSprite <= 0 || nSprite > g_pools.sprites)  // Hyperion: a save's id past the pool is none
    return 0;
  return &sgSprites[nSprite - 1];
}

// CSprite::GetSprite's exact inverse: NULL maps to 0 and every other pointer to its
// 1-based pool index. The divide is unsigned -- 1.17 reduces `/ 0x24` to the unsigned
// magic-number sequence -- so the difference is carried in an unsigned temporary before the
// divide; a signed pointer subtraction would emit `imul` and `sar` instead.
DWORD CSprite::GetSpriteID(CSprite *pSprite) {
  if (pSprite == 0)
    return 0;
  DWORD_PTR off = reinterpret_cast<DWORD_PTR>(pSprite);
  off -= reinterpret_cast<DWORD_PTR>(sgSprites);
  return (DWORD)(off / sizeof(CSprite) + 1);
}

// The draw-order sort key.
//           The three shifts cascade (13, then 5, then 9, summing to 27) rather than each
//           term being shifted into place on its own, so the source is the same
//           left-to-right accumulation. `elev <= 4 ? y : 0` and not the reverse: 1.17 jumps
//           on the negated condition. wPosY is sign-extended, which is what corrected its
//           declared type (src117/h/CSprite.h).
sprite_key_t CSprite::GetSpritePriority() {
  DWORD nPriority = this->bElevationLevel;
  DWORD nRow = (nPriority <= 4) ? static_cast<int>(this->wPosY) : 0;

#if !defined(_M_IX86)
  // Hyperion: a game past 1.17's limits has sprite indexes and rows past 1.17's 13
  // bits each, a row past 8,191 pixels spilling into the elevation's: the same fields in
  // the same order, wider. The index holds 20 bits, the flag one, the row 24 with
  // a bias for a row above the map, the elevation the bits above. A 1.17 game keeps 1.17's
  // key, so it draws as 1.17 does.
  if (HxLimits()) {
    const sprite_key_t qRow = static_cast<sprite_key_t>((static_cast<int>(nRow) + 0x8000) & 0xFFFFFF);
    return static_cast<sprite_key_t>(this->bElevationLevel) << 45 | qRow << 21 |
           static_cast<sprite_key_t>((this->flags.b & 0x10) != 0) << 20 | this->wIndex;
  }
#endif
  nPriority = (nPriority << 13) | nRow;
  nPriority = (nPriority << 5) | (this->flags.b & 0x10);
  nPriority = (nPriority << 9) | this->wIndex;
  return nPriority;
}

// The bounding box of every visible image
//           (flags bit 5, 0x20), written through the caller's rect. Two loops: the first
//           only searches, the seeding GetDimensions call sits between them, and the second
//           widens. The four compares are signed, in left, right, top, bottom order, which is
//           what makes rect32_t's members `long`.
void CSprite::GetSpriteRect(rect32_t *prc) {
  CImage *pImage = this->pImageHead;

  while (pImage) {
    if (pImage->flags.w & 0x20)
      break;
    pImage = pImage->pNext;
  }

  if (!pImage)
    return;

  pImage->GetDimensions(prc);

  for (pImage = pImage->pNext; pImage; pImage = pImage->pNext) {
    if (pImage->flags.w & 0x20) {
      rect32_t rc;

      pImage->GetDimensions(&rc);
      if (rc.left < prc->left)
        prc->left = rc.left;
      if (rc.right > prc->right)
        prc->right = rc.right;
      if (rc.top < prc->top)
        prc->top = rc.top;
      if (rc.bottom > prc->bottom)
        prc->bottom = rc.bottom;
    }
  }
}

// Copy the main image's two LO_SPECIAL(3)
//           overlay points -- the twin spawn offsets -- out through the caller's two
//           pointers. `ret 8h` => two dword slots. Both copies read through the pointer
//           CImage::GetOverlayOffset returns, which is what types that return
//           shield_offset_t *.
void CSprite::GetTwinSpawnOffsets(shield_offset_t *pOffs0,
                                  shield_offset_t *pOffs1) {
  shield_offset_t off;
  shield_offset_t *pOff;

  pOff = this->pMainImage->GetOverlayOffset(&off, 3, 0);
  pOffs0->x = pOff->x;
  pOffs0->y = pOff->y;

  pOff = this->pMainImage->GetOverlayOffset(&off, 3, 1);
  pOffs1->x = pOff->x;
  pOffs1->y = pOff->y;
}

// Flag early-out into a sibling, else a
//           range-guarded walk that steps each image's palette type down by 3. The
//           arithmetic happens at byte width -- `sub al, 3h` and only then the widening --
//           which the cast back to BYTE is what keeps. The member is read directly in both
//           compares; a named BYTE local makes MSVC range-reduce the pair.
void CSprite::Hide() {
  if (this->flags.b & 0x40) {
    this->sub_415E50(3, 0, 0);
    return;
  }

  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->bPaletteType >= 5 && pImage->bPaletteType <= 7)
      pImage->SetRLE((BYTE)(pImage->bPaletteType - 3));
}

// Hide every image that is NOT flagged
//           draw-while-cloaked. Two levels of inlining, so 1.17 shows a rect
//           build in the middle of an image walk with no calls but invalidate_rect:
//           CImage::Hide's body is inlined here, and CImage::Invalidate inside that. Hide's
//           own out-of-line copy still calls Invalidate, because in its own TU Invalidate
//           is not defined.
void CSprite::HideCloaked() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (!img_drawIfCloaked[pImage->wImageID]) {
      // --- CImage::Hide, inlined ---
      if (!pImage->flags.b.Hidden) {
        pImage->Invalidate();
        pImage->flags.b.Hidden = 1;
      }
    }
  }
}

// Hide the powerup image: ids [0x188,0x194]
//           first, then [0x3BE,0x3BF]. The hit tail-jumps CImage::Hide, which pins the
//           return type to void.
//
//           One walker variable across both loops, and one `goto`: that is what puts the
//           walker in ECX both times, which is the register CImage::Hide's `this` needs.
//           The head is read as `this->pImageHead` in both loops, not hoisted.
void CSprite::HidePowerup() {
  CImage *pImage;
  for (pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->wImageID >= 0x188 && pImage->wImageID <= 0x194)
      goto found;
  for (pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->wImageID >= 0x3BE && pImage->wImageID <= 0x3BF)
      goto found;
  return;
found:
  pImage->Hide();
}

// Place a fresh sprite: reject it off-map,
//           fill the fields, give it its type's image, cache the GRP extents.
//
//           The clamp is a ternary on the store. flags is overwritten with 0x20 inside the
//           sprite_visible guard, and that store has to precede the SetMask call, which could
//           read the byte. Bounds are compared at 16-bit unsigned width.
BOOL CSprite::Init(int nSpriteID, upos_t x, upos_t y, BYTE bPlayer) {
  if (x >= gwMapPixWidth)
    return 0;
  if (y >= gwMapPixHeight)
    return 0;

  this->bCreator = bPlayer;
  this->wSpriteID = (WORD)nSpriteID;
  this->bElevationLevel = 4;
  this->flags.b = 0;
  this->wPosX = x;
  this->wPosY = y;
  this->bVisibilityFlags = 0xFF;
  VisionHxSpriteSet(this, PlayerSet::All());  // Hyperion: seen by the slots past 7 too
  this->bFlashCounter = 0;

  if (!sprite_visible[nSpriteID]) {
    this->flags.b = 0x20;
    this->SetMask(0);
  }

  if (!this->AllocImage(guwSpriteImage[nSpriteID], 0, 0, 0))
    return 0;

  WORD w = this->pMainImage->pGroup->wMaxWidth;
  this->bGrpWidth = (BYTE)(w > 0xFF ? 0xFF : w);

  WORD h = this->pMainImage->pGroup->wMaxHeight;
  this->bGrpHeight = (BYTE)(h > 0xFF ? 0xFF : h);

  return 1;
}

// Hyperion: the sizes Init clamped, by Hyperion's rules from the group (CSprite.h).
int SpriteGrpWidth(const CSprite *pSprite)
{
  if (pSprite->bGrpWidth == 0xFF && HxRules() && pSprite->pMainImage != NULL && pSprite->pMainImage->pGroup != NULL)
    return pSprite->pMainImage->pGroup->wMaxWidth > 0xFF ? pSprite->pMainImage->pGroup->wMaxWidth : 0xFF;
  return pSprite->bGrpWidth;
}

int SpriteGrpHeight(const CSprite *pSprite)
{
  if (pSprite->bGrpHeight == 0xFF && HxRules() && pSprite->pMainImage != NULL && pSprite->pMainImage->pGroup != NULL)
    return pSprite->pMainImage->pGroup->wMaxHeight > 0xFF ? pSprite->pMainImage->pGroup->wMaxHeight : 0xFF;
  return pSprite->bGrpHeight;
}

// Reset the visibility hash and rebuild its per-sprite-type enable table. Static (see
// src117/h/CSprite.h). Each table is tested at byte width because only bit 0 matters; the
// index scale, 4 and 2, is the element stride and fixes each table's width. 0xE4 = 228 is
// units.dat's row count.
void CSprite::InitHash() {
  sgbVisHashEnabled_g = 0;
  memset(sgbVisHashBits_g, 0, 256);
  sgbVisHashSeq_g = 0;
  sgdwVisHashIndex_g = 0;

  for (DWORD i = 0; UnitTypeValid(i); i = UnitTypeNext(i)) {  // Hyperion: and a mod's types
    if (!(gulUnitTypeFlags[i] & 1) && (gfEditFlags[i] & 1))
      sgbVisHashTypeEnabled_g[guwFlingySprite[gubUnitFlingy[i]]] = 1;
  }
}

// Load sprites.dat, clear the pool and both
//           y-axis row tables, then thread every pool entry but the first onto the free list
//           with an ascending index.
//
//           The loop keeps three induction variables (a spilled down-counter, an index for
//           wIndex and a pointer); a plain `for` over sgSprites[i] becomes one pointer walk.
//           sprites_sgpFreeHead is re-read each iteration, since AppendLink moves it.
void CSprite::InitSprites() {
  load_array_data("arr\\sprites.dat", gSPRITESArrayDataTbl_g);

  memset(sgSprites, 0, g_pools.sprites * sizeof(CSprite));  // Hyperion: 0x15F90 in 1.17
  sprites_sgpFreeTail = &sgSprites[0];
  sprites_sgpFreeHead = &sgSprites[0];
  sgSprites[0].wIndex = 0;
  memset(g_spriteRowHead, 0, sizeof(g_spriteRowHead));  // Hyperion: 0x400 in 1.17
  memset(g_spriteRowTail, 0, sizeof(g_spriteRowTail));

  int n = g_pools.sprites - 1;
  int i = 1;
  CSprite *pSprite = &sgSprites[1];
  do {
    pSprite->wIndex = static_cast<sprite_index_t>(i);
    reinterpret_cast<CLink *>(sprites_sgpFreeHead)->AppendLink(
        reinterpret_cast<CLink *>(pSprite),
        reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
    i++;
    pSprite++;
  } while (--n);
}

// Dirty this image's on-screen rectangle.
//           A CImage member in CSprite.cpp's TU, as in 1.17, which lets
//           CSprite::ForceInvalid and CSprite::HideCloaked inline it. Three guards, in
//           1.17's order: the global suppression bit, the height (+0x2A), the width (+0x28).
//           The `short` locals give the zero-extending load and the sign-extension at the add.
void CImage::Invalidate() {
  rect32_t rc;

  if (byte_6D0BF5 & 1)
    return;

  short h = this->wDrawHeight;
  if (h <= 0)
    return;

  short w = this->wDrawWidth;
  if (w <= 0)
    return;

  rc.left   = this->wScreenX;
  rc.top    = this->wScreenY;
  rc.right  = this->wScreenX + w;
  rc.bottom = this->wScreenY + h;
  invalidate_rect(&rc);
}

// Recompute this sprite's draw info,
//           invalidating around the change: a dirty image gets its old screen rect
//           invalidated, then DrawUpdateRect, then its new rect; a clean one gets the
//           recompute alone. The two rect blocks are not common subexpressions, since
//           DrawUpdateRect rewrites all four fields between them.
void CSprite::InvalidateDrawInfo() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->flags.w & 1) {
      short h = pImage->wDrawHeight;
      if (h > 0) {
        short w = pImage->wDrawWidth;
        if (w > 0) {
          rect32_t rc;
          rc.left   = pImage->wScreenX;
          rc.top    = pImage->wScreenY;
          rc.right  = pImage->wScreenX + w;
          rc.bottom = pImage->wScreenY + h;
          invalidate_rect(&rc);
        }
      }
      pImage->DrawUpdateRect();
      short h2 = pImage->wDrawHeight;
      if (h2 > 0) {
        short w2 = pImage->wDrawWidth;
        if (w2 > 0) {
          rect32_t rc2;
          rc2.left   = pImage->wScreenX;
          rc2.top    = pImage->wScreenY;
          rc2.right  = pImage->wScreenX + w2;
          rc2.bottom = pImage->wScreenY + h2;
          invalidate_rect(&rc2);
        }
      }
    } else {
      pImage->DrawUpdateRect();
    }
  }
}

// CSprite::InvalidateDrawInfo's per-image
//           body, run over every sprite in an inclusive range of y-axis rows. The range is
//           inclusive at both ends and the counter unsigned, which is what `<=` on two
//           DWORD parameters gives. The body is written out rather than called, since
//           InvalidateDrawInfo is a per-sprite member and this loop needs its per-image
//           half.
void CSprite::InvalidateSprites(DWORD yTop, DWORD yBottom) {
  for (DWORD y = yTop; y <= yBottom; y++) {
    for (CSprite *pSprite = g_spriteRowHead[y]; pSprite;
         pSprite = pSprite->pNext) {
      for (CImage *pImage = pSprite->pImageHead; pImage; pImage = pImage->pNext) {
        if (pImage->flags.w & 1) {
          short h = pImage->wDrawHeight;
          if (h > 0) {
            short w = pImage->wDrawWidth;
            if (w > 0) {
              rect32_t rc;
              rc.left   = pImage->wScreenX;
              rc.top    = pImage->wScreenY;
              rc.right  = pImage->wScreenX + w;
              rc.bottom = pImage->wScreenY + h;
              invalidate_rect(&rc);
            }
          }
          pImage->DrawUpdateRect();
          short h2 = pImage->wDrawHeight;
          if (h2 > 0) {
            short w2 = pImage->wDrawWidth;
            if (w2 > 0) {
              rect32_t rc2;
              rc2.left   = pImage->wScreenX;
              rc2.top    = pImage->wScreenY;
              rc2.right  = pImage->wScreenX + w2;
              rc2.bottom = pImage->wScreenY + h2;
              invalidate_rect(&rc2);
            }
          }
        } else {
          pImage->DrawUpdateRect();
        }
      }
    }
  }
}

// Is this screen point over any visible
//           image of this sprite? `ret 8h` => two dword slots. The two exits are separate
//           blocks returning a full dword, so the return is `int` and not a byte.
int CSprite::IsClickablePt(int x, int y) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if ((pImage->flags.w & 0x20) && pImage->ClickableAt(x, y))
      return 1;
  }
  return 0;
}

// Kill every image, then move this sprite
//           off its y-axis row and onto the free list.
//
//           The walk saves the next link before the call, because CImage::Kill unlinks the
//           node it is given. The tile arithmetic is CSprite::GetYTile written out:
//           GetYTile is defined later in this file, so it could not be inlined. The divide
//           by 32 is signed, which is what makes the `< 0` arm reachable at all.
void CSprite::Kill() {
  CImage *pImage = this->pImageHead;
  while (pImage) {
    CImage *pNext = pImage->pNext;
    pImage->Kill();
    pImage = pNext;
  }

  int nTile = this->wPosY / 32;
  if (nTile < 0)
    nTile = 0;
  else if (nTile >= gwMapHeight)
    nTile = gwMapHeight - 1;

  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&g_spriteRowHead[nTile]),
      reinterpret_cast<CLink **>(&g_spriteRowTail[nTile]));
  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&sprites_sgpFreeHead),
      reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
}

// The saved sprite record: a 0-based pool index, then the raw 36-byte CSprite. Both codecs
// size it with `imul ..., 26h` and copy the payload whole.
struct sprite_saveentry_t {
  /*0x00*/ WORD wSpriteId;
  /*0x02*/ BYTE bSprite[sizeof(CSprite)];
};
static_assert(offsetof(sprite_saveentry_t, wSpriteId) == 0x00, "sprite_saveentry_t::wSpriteId");
static_assert(offsetof(sprite_saveentry_t, bSprite) == 0x02, "sprite_saveentry_t::bSprite");
OC_X86_LAYOUT(sizeof(sprite_saveentry_t) == 0x26, "sprite_saveentry_t size");

// 0x4BE950 with the sprite in ECX; one of gen's folded-alias spellings.
void __fastcall voice_squelch(void *);

// Reads the sprite pool back from a save: the packed records, then both y-axis
// row lists, then renumbers every slot. The two late pfread failures share the
// decompress failure's `return 0`.
BOOL CSprite::LoadSprites(void *pFile) {
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(g_pools.sprites);
  DWORD wCount = 0;
  if (!pfread(&wCount, nSlotBytes, pFile))
    return 0;

  memset(sgSprites, 0, g_pools.sprites * sizeof(CSprite));  // Hyperion: 0x15F90 in 1.17

  if (wCount != 0) {
    // Hyperion: a record is the index and the sprite in 1.17's layout,
    // sprite_saveentry_t on x86.
    DWORD nBufSize = wCount * (nSlotBytes + PortSave117Size(SAVE117_CSprite));
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nBufSize, "CSprite.cpp", 1898, 0));
    const BYTE *pEntry = pBuf;
    memset(pBuf, 0, nBufSize);
    if (!decompress(pBuf, nBufSize, pFile)) {
      SMemFree(pBuf, "CSprite.cpp", 1904, 0);
      return 0;
    }

    for (DWORD i = 0; i < wCount; i++) {
      const unsigned wSlot = PoolSaveSlot(pEntry, nSlotBytes);
      // Hyperion: a slot past the game's pool is a broken save
      if (wSlot >= static_cast<unsigned>(g_pools.sprites)) {
        SMemFree(pBuf, "CSprite.cpp", 0, 0);
        return 0;
      }
      CSprite *pSprite = &sgSprites[wSlot];
      pEntry = PortSave117Get(SAVE117_CSprite, pEntry + nSlotBytes, pSprite);
      voice_squelch(pSprite);
    }

    SMemFree(pBuf, "CSprite.cpp", 1922, 0);
  }

  DecodeListFromSave();
  sub_416AE0();

  // Hyperion: 0x400 bytes, a row for each of the map's past 256
  DWORD nSavedRows = gwMapHeight > 256 ? gwMapHeight : 256;
  if (!PortSaveReadIds(pFile, g_spriteRowHead, nSavedRows))
    return 0;
  DWORD i = gwMapHeight;
  while (i != 0) {
    CSprite *pSprite = g_spriteRowHead[i - 1];
    i--;
    g_spriteRowHead[i] = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(pSprite));
  }

  if (!PortSaveReadIds(pFile, g_spriteRowTail, nSavedRows))
    return 0;
  i = gwMapHeight;
  while (i != 0) {
    CSprite *pSprite = g_spriteRowTail[i - 1];
    i--;
    g_spriteRowTail[i] = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(pSprite));
  }

  // Hyperion: wIndex by name, +0x10 of 0x24-byte strides in 1.17.
  for (int wId = 0; wId < g_pools.sprites; wId++)
    sgSprites[wId].wIndex = static_cast<sprite_index_t>(wId);

  return 1;
}

// First image whose id falls in
//           [first, last], else NULL. Both compares are unsigned against 32-bit registers,
//           so the parameters are 32-bit unsigned: a WORD field against `int` parameters
//           would promote and emit `jl` and `jle`.
CImage *CSprite::OverlayGetImage(DWORD nFirstImageID, DWORD nLastImageID) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->wImageID >= nFirstImageID && pImage->wImageID <= nLastImageID)
      return pImage;
  return 0;
}

// Set flag 0x0004 on the TAIL image, palette
//           type 10 only. Bit 2 of the flags word is not named in
//           src117/h/CImage.h -- no body has yet shown what it means -- so the
//           whole-word arm is used rather than inventing a name.
void CSprite::sub_416970() {
  CImage *pImage = this->pImageTail;
  if (pImage->bPaletteType == 10)
    pImage->flags.w |= 4;
}

// Is this map point inside the sprite's
//           bounding box? A short-circuit chain whose four early-outs share one exit, in
//           1.17's order: x against left, y against top, x against right, y against
//           bottom. The compares are signed, hence `long` parameters and rect32_t members.
//           MSVC does not inline CSprite::GetSpriteRect here, on size, nor did 1.17's.
int CSprite::IsUnderPoint(int x, int y) {
  rect32_t rc;

  this->GetSpriteRect(&rc);
  if (x >= rc.left && y >= rc.top && x < rc.right && y < rc.bottom)
    return 1;
  return 0;
}

// Find the team-colour circle and unlink it.
//           The walk runs backward, from pImageTail through CImage::pPrev. It returns the
//           found image's colouring byte, or 0 when there is none.
BYTE CSprite::RemoveTeamCircle() {
  for (CImage *pImage = this->pImageTail; pImage; pImage = pImage->pPrev) {
    if (pImage->wImageID >= 0x23B && pImage->wImageID <= 0x244) {
      BYTE bColoringData = pImage->coloringData.b;
      pImage->RemoveCircle();
      return bColoringData;
    }
  }
  return 0;
}

// Rebuild the draw-priority list from the
//           y-axis rows in [yTop, yBottom], then run the sight hash if it has been armed.
//           Two walks, chosen on gPlayback before either: the live arm admits a sprite whose
//           visibility byte meets the local player's mask, the replay arm uses the replay
//           mask and also admits creator 11 when "show all" is on. SightHashUpdate is called,
//           not inlined (its __declspec(noinline)).
void CSprite::PriorityAddRange(DWORD yTop, DWORD yBottom) {
  priority_init();

  if (gPlayback == 0) {
    for (DWORD y = yTop; y <= yBottom; y++) {
      for (CSprite *pSprite = g_spriteRowHead[y]; pSprite;
           pSprite = pSprite->pNext) {
        if (pSprite->bVisibilityFlags & (BYTE)gdwLocalMask)
          priority_add(pSprite);
      }
    }
  } else {
    for (DWORD y = yTop; y <= yBottom; y++) {
      for (CSprite *pSprite = g_spriteRowHead[y]; pSprite;
           pSprite = pSprite->pNext) {
        if ((pSprite->bVisibilityFlags & (BYTE)gdwReplayVisionMask) ||
            (sgbShowAll_g && pSprite->bCreator == 0x0B) ||
            ViewHxSpriteSeen(pSprite))  // Hyperion: the viewer's slots past 7 (stage D)
          priority_add(pSprite);
      }
    }
  }

  if (sgbVisHashEnabled_g) {
    sgbVisHashEnabled_g = 0;
    CSprite::SightHashUpdate(yTop);
  }
}

// Pixel Y to tile row, clamped to the map.
//           The divide by 32 is signed, which is what makes the `nTile < 0` arm reachable
//           at all, and the clamp is to gwMapHeight - 1, an exclusive row count.
int CSprite::GetYTile() {
  int nTile = this->wPosY / 32;
  if (nTile < 0)
    return 0;
  if (nTile >= gwMapHeight)
    return gwMapHeight - 1;
  return nTile;
}

// The walk, guarded on an equality test.
void CSprite::RedrawHP() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->bPaletteType == 0x0B)
      pImage->flags.b.Dirty = 1;
}

// Stage the eight cloak remap rows for one
//           player's colour into the block at dword_508890 + 0x800, which rle_draw_effect
//           then indexes. Only palette types 5, 6 and 7 proceed, an 8-bit range test kept by
//           the cast back to BYTE. The row table is indexed [colour][row] at an 8-byte
//           stride, and each byte it yields is a 256-byte page index into the source block.
void prepare_visible_cloak_draw(BYTE bPaletteType, BYTE bPlayer) {
  if ((BYTE)(bPaletteType - 5) > 2)
    return;

  BYTE nColor = get_unit_color_override(bPlayer);
  BYTE *pDst  = reinterpret_cast<BYTE *>(dword_508890);
  BYTE *pBase = pDst;
  pDst += 0x800;

  for (int i = 0; i < 8; i++)
    memcpy(pDst + i * 0x100, pBase + (byte_529056[nColor][i] << 8), 0x100);
}

// Clear the team-circle counter and remove
//           the circle, but only if the counter was non-zero.
//
//           The guard is the bits 1..2 bitfield, nTeamCircleTTL; a zero-test of a 2-bit
//           field needs no extract, so this reads `test al, 6h` where
//           ReleaseTeamCircle below, which decrements, has to shift first. The tail is
//           CSprite::RemoveTeamCircle inlined -- it is defined earlier in this file.
BYTE CSprite::RemoveTeamCircleIf() {
  if (!this->flags.bits.nTeamCircleTTL)
    return 0;
  this->flags.bits.nTeamCircleTTL = 0;
  return this->RemoveTeamCircle();
}

// Tick the team-circle countdown, remove the
//           circle at zero. The test is an unsigned `<= 0`, not `== 0` (1.17 branches on
//           `jbe`), and the trailing RemoveTeamCircle() becomes a conditional tail jump.
void CSprite::ReleaseTeamCircle() {
  if (this->flags.bits.nTeamCircleTTL != 0) {
    --this->flags.bits.nTeamCircleTTL;
    if (this->flags.bits.nTeamCircleTTL <= 0)
      this->RemoveTeamCircle();
  }
}

// Select this sprite: record the selection
//           index, set the flag, allocate the health bar and the selection circle. Both
//           parameters are consumed as one byte; the tail is CSprite::AllocSelect inlined.
//           The index parameter is read before the flags are set, which keeps AL live and
//           gives 1.17's `mov ecx, esi` reload before AllocHealthbar.
void CSprite::SelectAdd(BYTE bSelectionIndex, BYTE bSelIndex) {
  if (this->flags.b & 8)
    return;
  this->bSelectionIndex = bSelectionIndex;
  this->flags.b |= 8;
  CImage::AllocHealthbar(this);
  this->AllocSelect(bSelIndex);
}

// Early-out on an unchanged value, then a
//           bare walk. The argument is read as a BYTE and compared byte-to-byte
//           against bCreator, so the parameter is BYTE-typed even though
//           `ret 4h` cleans a full dword slot.
void CSprite::SetCreator(BYTE bPlayer) {
  if (this->bCreator != bPlayer) {
    this->bCreator = bPlayer;
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
      pImage->flags.b.Dirty = 1;
  }
}

// Byte-for-byte the same 37-instruction
//           stream as CSprite::FaceDirection above except for the callee name.
void CSprite::SetFacing(int nDirection) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->SetFacing(nDirection);
}

// Guarded walk, the call before the store.
//           The guard runs the body on `pt == 0 || pt == 0x10`. The store is a dword of a
//           zero-extended byte, the parameter held in a callee-saved register as a BYTE so
//           that it survives the SetRLE call; zero-extension is what makes it unsigned.
void CSprite::SetShift(BYTE bShift) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (pImage->bPaletteType == 0 || pImage->bPaletteType == 0x10) {
      pImage->SetRLE(0x10);
      pImage->coloringData.dw = bShift;
    }
  }
}

// Set Dirty on every image.
void CSprite::MakeDirty() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->flags.b.Dirty = 1;
}

// Walk with a two-term guard,
//           `bForce || !(flags & 0x80)`, whose second term reads a field of `this`, re-read
//           every iteration since the call can move it. Two nested ifs, or the inverted
//           `continue` form, flip the branch senses.
void CSprite::SetAnimation(BYTE bAnim, int bForce) {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (bForce || !(this->flags.b & 0x80))
      pImage->SetAnimation(bAnim);
}

// One image, one guard, one tail call to
//           CImage::SetVerticalOffset. There is no null check on pImageTail: 1.17
//           dereferences it unconditionally. `y` is `char` and forwarded untouched in its
//           incoming slot; widening it to `int` needs a conversion, which loses the jump.
void CSprite::SetShadowOffset(char y) {
  CImage *pImage = this->pImageTail;
  if (pImage->bPaletteType == 10)
    pImage->SetVerticalOffset(y);
}

// Re-mask this sprite's per-player visibility byte, repainting only when the masked value
// changes. The store is unconditional and the guard only chooses the repaint: an unchanged
// masked value still writes the byte, because the bits outside the mask can differ. The
// compare names the field first, which is the operand order 1.17 has.
void CSprite::SetMask(BYTE bVisibility) {
  if ((this->bVisibilityFlags & gdwLocalMask) != (bVisibility & gdwLocalMask))
    SpriteViewRedraw(this, (bVisibility & gdwLocalMask) != 0);

  this->bVisibilityFlags = bVisibility;
  if (bVisibility == 0)
    VisionHxSpriteSet(this, PlayerSet::None());  // Hyperion: hidden from the slots past 7 too
}

// Hyperion: SetMask's redraw, which the replay viewer's slots past 7 call too
// (playervision.h): a sprite gone from view invalidates where its images
// were, one come into view marks them for drawing.
void SpriteViewRedraw(CSprite *pSprite, bool bSeen) {
  if (!bSeen) {
    for (CImage *pImage = pSprite->pImageHead; pImage; pImage = pImage->pNext) {
      // --- CImage::Invalidate, inlined ---
      rect32_t rc;

      if (byte_6D0BF5 & 1)
        continue;

      short hh = pImage->wDrawHeight;
      if (hh <= 0)
        continue;

      short ww = pImage->wDrawWidth;
      if (ww <= 0)
        continue;

      rc.left = pImage->wScreenX;
      rc.top = pImage->wScreenY;
      rc.right = pImage->wScreenX + ww;
      rc.bottom = pImage->wScreenY + hh;
      invalidate_rect(&rc);
    }
  } else {
    for (CImage *pImage = pSprite->pImageHead; pImage; pImage = pImage->pNext)
      pImage->flags.b.Dirty = 1;
  }
}

// Two arms over the same image walk, chosen
//           by sprite flags bit 6 (0x40). The member is read directly in both compares of
//           each range, which keeps them two compares, and `(BYTE)(... + 3)` adds at byte
//           width before widening, as 1.17 does.
void CSprite::Show() {
  if (this->flags.b & 0x40) {
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
      if (pImage->bPaletteType >= 2 && pImage->bPaletteType <= 7)
        pImage->SetRLE(0);
  } else {
    for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
      if (pImage->bPaletteType >= 2 && pImage->bPaletteType <= 4)
        pImage->SetRLE((BYTE)(pImage->bPaletteType + 3));
      else if (pImage->bPaletteType == 0)
        pImage->SetRLE(6);
    }
  }
}

// The inverse of HideCloaked: un-hide every
//           image that is not flagged draw-while-cloaked. CImage::Show is written out --
//           it lives in another TU, so it cannot be inlined from a call -- and its whole
//           21-byte body is these three statements. The named bits are used rather than
//           magic constants: clear Hidden, set Dirty.
void CSprite::ShowCloaked() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext) {
    if (!img_drawIfCloaked[pImage->wImageID]) {
      // --- CImage::Show, inlined ---
      if (pImage->flags.b.Hidden) {
        pImage->flags.b.Hidden = 0;
        pImage->flags.b.Dirty = 1;
      }
    }
  }
}

// Walk guarded on an unsigned range,
//           2 <= pt <= 7, written as a single `&&` for 1.17's fallthrough sense.
void CSprite::ShowNormal() {
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->bPaletteType >= 2 && pImage->bPaletteType <= 7)
      pImage->SetRLE(0);
}

// The inverse of CSprite::HidePowerup, over
//           the same two id bands. The show block is written once and reached by `goto`
//           from both walks: MSVC does not tail-merge a 21-byte tail, so a copy in each
//           loop body is a second copy in the object. The block is CImage::Show written
//           out, as in CSprite::ShowCloaked.
void CSprite::ShowPowerup() {
  CImage *pImage;
  for (pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->wImageID >= 0x188 && pImage->wImageID <= 0x194)
      goto found;
  for (pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    if (pImage->wImageID >= 0x3BE && pImage->wImageID <= 0x3BF)
      goto found;
  return;
found:
  // --- CImage::Show, inlined (see CSprite::ShowCloaked's banner) ---
  if (pImage->flags.b.Hidden) {
    pImage->flags.b.Hidden = 0;
    pImage->flags.b.Dirty = 1;
  }
}

// Recompute the whole visibility hash, then
//           fold a 14-row window of the priority hash table starting at nRow into the
//           one-byte sequence check. Static: ECX is a row index into a 256-byte table, not
//           a `this`. The __declspec(noinline), repeated on src117/h/CSprite.h's
//           declaration, keeps /Ob2 from splicing it into CSprite::PriorityAddRange.
__declspec(noinline) void CSprite::SightHashUpdate(DWORD nRow) {
  memset(sgbVisHashBits_g, 0, 256);
  // Hyperion: a map past 256 rows folds its rows onto the hash's 256 bytes, which
  // the multiplayer sync sends; 1.17's maps hash as they always did
  DWORD nRows = gwMapHeight > 256 ? gwMapHeight : 256;

  DWORD i;
  DWORD nLast;
  DWORD dwHash;
  CSprite *pSprite;
  BYTE *pBits;

  for (i = 0; i < nRows; i++) {
    pBits = &sgbVisHashBits_g[i & 0xFF];
    pSprite = g_spriteRowHead[i];
    while (pSprite != 0) {
      if (sgbVisHashTypeEnabled_g[pSprite->wSpriteID] != 0) {
        BYTE bVis = pSprite->bVisibilityFlags;
        *pBits ^= bVis;
      }
      pSprite = pSprite->pNext;
    }
  }

  nLast = nRow + 13;
  if (nLast >= nRows)
    nLast = nRows - 1;

  dwHash = 0;
  for (i = nRow; i <= nLast; i++) {
    DWORD b = g_spritePriorityVisHash[i];
    dwHash ^= b;
    dwHash = (dwHash << 1) | (dwHash >> 31);
  }

  sgdwVisHashIndex_g = nRow;
  dwHash ^= dwHash >> 16;
  sgbVisHashSeq_g = (BYTE)(dwHash >> 8) ^ (BYTE)dwHash;
}

// Snapshot the visibility hash into a caller
//           record: 256 bytes, in a record of stride 268 = 12 + 256. The sequence byte is
//           read before the copy and written after it, as two statements; either order
//           around the memcpy puts the load and the store on the same side.
void SnapshotSightHash(BYTE *pbSeq, DWORD *pdwIndex, BYTE *pbBits) {
  BYTE bSeq = sgbVisHashSeq_g;
  memcpy(pbBits, sgbVisHashBits_g, 256);
  *pbSeq = bSeq;
  *pdwIndex = sgdwVisHashIndex_g;
}

// Stores 1 to sgbVisHashEnabled_g, a 4-byte
//           store, which is the global's width.
void EnableVisibilityHashUpdate() {
  sgbVisHashEnabled_g = 1;
}

// Spawn one dust thingy per overlay slot in [nFirstOverlay, nLastOverlay] that has an offset.
// The position goes through an int array: plain locals fold each read into one movsx,
// where the oracle loads both words with movzx and sign-extends later. wPosY is read unsigned.
void CSprite::SpawnDust(int nLoType, int nFirstOverlay, int nLastOverlay,
                        int nThingyType, BYTE bAbove) {
  for (int i = nFirstOverlay; i <= nLastOverlay; i++) {
    shield_offset_t off;
    pMainImage->GetOverlayOffset(&off, (BYTE)nLoType, i);
    if (off.x != 0x7F || off.y != 0x7F) {
      int pos[2] = { wPosX, PosU(wPosY) };
      CThingy *pThingy = CThingy::AllocThingy(nThingyType + i - nFirstOverlay,
                                              PosS(pos[0]) + (char)off.x,
                                              PosS(pos[1]) + (char)off.y, 0);
      if (pThingy != NULL) {
        pThingy->pSprite->bElevationLevel = bElevationLevel + 1;
        pThingy->UpdateVisibility();
        if (bAbove) {
          for (CImage *pImage = pThingy->pSprite->pImageHead; pImage; pImage = pImage->pNext)
            pImage->FlipImage(1);
        }
      }
    }
  }
}

// The switch is over the main image's current animation (CImage+0x16, values 2..15) and
// maps it to the matching *ToIdle animation: 2 and 5 -> 8; 3 and 6 -> 9; 7 -> 12; 13 -> 12
// guarded on wSpriteID 0x1E9; 15 -> 8 guarded on wSpriteID in {0xEF, 0x8C, 0xC2}. The
// `case 13:` arm falls through into `case 7:`, which is what the jump table has; two
// separate `bAnim = 12; break;` arms tail-merge the other way round.
void CSprite::SwitchToIdle() {
  BYTE bAnim;
  switch (this->pMainImage->bAnim) {  // +0x16
  case 2:
  case 5:
    bAnim = 8;
    break;
  case 3:
  case 6:
    bAnim = 9;
    break;
  case 15:
    if (this->wSpriteID != 0xEF &&
        this->wSpriteID != 0x8C &&
        this->wSpriteID != 0xC2)
      return;
    bAnim = 8;
    break;
  case 13:
    if (this->wSpriteID != 0x1E9)
      return;
    // fall through
  case 7:
    bAnim = 12;
    break;
  default:
    return;
  }
  for (CImage *pImage = this->pImageHead; pImage; pImage = pImage->pNext)
    pImage->SetAnimation(bAnim);
}

// The column and row counts re-read the image through their own helpers, which keeps
// pImage in EBX and sends y0 to the frame, as 1.17 does.
static __forceinline int InvalCols(const CImage *pImage, int x0) {
  return (pImage->wScreenX - 1 + (short)pImage->wDrawWidth) / 16 - x0 + 1;
}

static __forceinline int InvalRows(const CImage *pImage, int y0) {
  return (pImage->wScreenY - 1 + (short)pImage->wDrawHeight) / 16 - y0 + 1;
}

// oracle 0x417790 (173 B). Whether any 16-pixel tile the image covers is
// marked dirty in the 40-wide invalidation grid; returns on the first nonzero
// byte. The row step is `mov edi, 28h`, so the stride 40 is the oracle's.
// Hyperion: the grid is the view's, as wide as its screen (port_view.cpp).
BOOL __fastcall IsInvalSet(CImage *pImage) {
  int x0, y0, cols, rows, skip;
  BYTE *p;

  x0 = pImage->wScreenX / 16;
  y0 = pImage->wScreenY / 16;
  cols = InvalCols(pImage, x0);
  rows = InvalRows(pImage, y0);
  skip = g_portView.nGridWidth - cols;
  p = &g_portView.pbInvalMap[y0 * g_portView.nGridWidth + x0];

  while (rows != 0) {
    rows--;
    int c = cols;
    while (c != 0) {
      c--;
      if (*p != 0)
        goto found;
      p++;
    }
    p += skip;
  }
  return 0;
found:
  return 1;
}

// The inverse of sub_416970.
//           The mask is materialised into a register rather than used as an immediate:
//           `and word ptr [mem], imm16` needs the 0x66 length-changing prefix, which MSVC
//           avoids. sub_416970's `or ..., 4h` keeps its immediate, an imm8 being
//           sign-extended instead.
void CSprite::sub_417840() {
  CImage *pImage = this->pImageTail;
  if (pImage->bPaletteType == 10)
    pImage->flags.w &= ~4;
}

// Run every image's update procedure and its
//           iscript, and recycle the sprite when the last image removes itself. pNext is
//           saved before the body because RunScript can destroy the node and empty the
//           list, and pImageHead is re-read inside the loop. GetYTile is called here, not
//           inlined.
void CSprite::Update() {
  CImage *pImage = this->pImageHead;
  while (pImage) {
    CImage *pNext = pImage->pNext;
    pImage->pfnUpdate(pImage);
    pImage->RunScript(reinterpret_cast<BYTE *>(&pImage->wIscriptHeader), 0, 0);  // &+0x10
    if (!this->pImageHead) {
      int nTile = this->GetYTile();
      reinterpret_cast<CLink *>(this)->Unlink(
          reinterpret_cast<CLink **>(&g_spriteRowHead[nTile]),
          reinterpret_cast<CLink **>(&g_spriteRowTail[nTile]));
      reinterpret_cast<CLink *>(this)->PrependThis(
          reinterpret_cast<CLink **>(&sprites_sgpFreeHead),
          reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
    }
    pImage = pNext;
  }
}

// The inverse of EncodeListForSave: every
//           1-based pool id stored in a pointer field is turned back into the pointer.
//           CImage::GetImage is a real call; GetSprite above is inlined.
void CSprite::DecodeListFromSave() {
  CSprite *p = sgSprites;
  DWORD n = g_pools.sprites;
  while (n != 0) {
    n--;
    p->pMainImage = CImage::GetImage((DWORD)reinterpret_cast<DWORD_PTR>(p->pMainImage));
    p->pImageHead = CImage::GetImage((DWORD)reinterpret_cast<DWORD_PTR>(p->pImageHead));
    p->pImageTail = CImage::GetImage((DWORD)reinterpret_cast<DWORD_PTR>(p->pImageTail));
    p->pPrev = GetSprite((int)reinterpret_cast<intptr_t>(p->pPrev));
    p->pNext = GetSprite((int)reinterpret_cast<intptr_t>(p->pNext));
    p++;
  }
}

// Every pointer field of every sprite rewritten in place as its 1-based pool id, ahead of
// the raw block write.
void CSprite::EncodeListForSave() {
  CSprite *p = sgSprites;
  DWORD n = g_pools.sprites;
  while (n != 0) {
    n--;
    p->pMainImage = reinterpret_cast<CImage *>((DWORD_PTR)CImage::GetImageID(p->pMainImage));
    p->pImageHead = reinterpret_cast<CImage *>((DWORD_PTR)CImage::GetImageID(p->pImageHead));
    p->pImageTail = reinterpret_cast<CImage *>((DWORD_PTR)CImage::GetImageID(p->pImageTail));
    p->pPrev = reinterpret_cast<CSprite *>((DWORD_PTR)GetSpriteID(p->pPrev));
    p->pNext = reinterpret_cast<CSprite *>((DWORD_PTR)GetSpriteID(p->pNext));
    p++;
  }
}

// Strips every sprite's selection state
//           ahead of a save: bit 3 -> Deselect(), else bit 0 -> RemoveCircle(); bits 1..2
//           (the team-circle TTL) cleared and the team-circle overlay image (0x23B..0x244)
//           removed, walking from pImageTail backwards.
void CSprite::RemoveSelectsForSave() {
  CSprite *p = sgSprites;
  DWORD n = g_pools.sprites;
  while (n != 0) {
    n--;
    BYTE f = p->flags.b;
    if (f & 8)
      p->Deselect();
    else if (f & 1)
      p->RemoveCircle();
    f = p->flags.b;
    if (f & 6) {
      CImage *pImage = p->pImageTail;
      p->flags.b = f & ~6;
      for (; pImage != 0; pImage = pImage->pPrev) {
        if (pImage->wImageID >= 0x23B && pImage->wImageID <= 0x244) {
          pImage->RemoveCircle();
          break;
        }
      }
    }
    p++;
  }
}

// Rebuilds the sprite free list after
//           a load: scan the pool for the first slot with a null pImageHead, seed head and
//           tail with it, then append every later free slot. 1.17's five-way compare ladder
//           is /Ox unrolling the scan by 5; the five displacements are sgSprites-relative
//           folds, not globals.
void sub_416AE0() {
  DWORD i;
  for (i = 0; i < g_pools.sprites && sgSprites[i].pImageHead != 0; i++)
    ;
  if (i == g_pools.sprites) {
    sprites_sgpFreeHead = 0;
    sprites_sgpFreeTail = 0;
    return;
  }
  CSprite *pPrev = &sgSprites[i];
  sprites_sgpFreeHead = pPrev;
  sprites_sgpFreeTail = pPrev;
  i++;
  if (i < g_pools.sprites) {
    CSprite *p = &sgSprites[i];
    DWORD n = g_pools.sprites - i;
    do {
      if (p->pImageHead == 0) {
        reinterpret_cast<CLink *>(pPrev)->AppendLink(
            reinterpret_cast<CLink *>(p),
            reinterpret_cast<CLink **>(&sprites_sgpFreeTail));
        pPrev = p;
      }
      p++;
    } while (--n != 0);
  }
}

// Clears the sprite's bit-0 "has
//           selection" flag, then hunts the image list from pImageTail backwards for a
//           selection-circle image (0x231..0x23A) and removes it, returning its colouring
//           byte. The `noinline` matters -- see src117/h/CSprite.h.
__declspec(noinline) BYTE CSprite::RemoveCircle() {
  BYTE bFlags = this->flags.b;
  if (!(bFlags & 1))
    return 0;
  this->flags.b = bFlags & 0xFE;
  CImage *pImage = this->pImageTail;
  if (pImage == 0)
    return 0;
  while (pImage != 0) {
    if (pImage->wImageID >= 0x231 && pImage->wImageID <= 0x23A) {
      BYTE bColor = pImage->coloringData.b;
      pImage->RemoveCircle();
      return bColor;
    }
    pImage = pImage->pPrev;
  }
  return 0;
}

// Early-outs when the position is
//           unchanged; re-tiles both halves and moves the sprite between the y-axis draw
//           rows when the row changed; finally re-dirties every image in the list. Both
//           parameters are WORD, from the callee's 16-bit entry reads.
void CSprite::SetPosition(upos_t x, upos_t y) {
  if (this->wPosX == x && PosU(this->wPosY) == y)
    return;
  int nOldRow = this->wPosY / 32;
  if (nOldRow < 0)
    nOldRow = 0;
  else if (nOldRow >= gwMapHeight)
    nOldRow = gwMapHeight - 1;
  this->wPosY = y;
  this->wPosX = x;
  int nNewRow = PosS(y) / 32;
  if (nNewRow < 0)
    nNewRow = 0;
  else if (nNewRow >= gwMapHeight)
    nNewRow = gwMapHeight - 1;
  if (nOldRow != nNewRow) {
    reinterpret_cast<CLink *>(this)->Unlink(
        reinterpret_cast<CLink **>(&g_spriteRowHead[nOldRow]),
        reinterpret_cast<CLink **>(&g_spriteRowTail[nOldRow]));
    reinterpret_cast<CLink *>(this)->PrependThis(
        reinterpret_cast<CLink **>(&g_spriteRowHead[nNewRow]),
        reinterpret_cast<CLink **>(&g_spriteRowTail[nNewRow]));
  }
  for (CImage *pImage = this->pImageHead; pImage != 0; pImage = pImage->pNext)
    pImage->flags.w |= 1;
}

// Restores the selection state after
//           a load: re-select every unit the save carried in gpNewUnits, then, in a team
//           game, walk the eight players downwards from player 7 and re-create the
//           team-circle overlay for every player whose group table entries survive and
//           whose lobby team equals the local player's.
// Hyperion: the selection's arrays through selectlimits.h's accessors (stage S).
void CSprite::RestoreSelectsFromSave() {
  for (int i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = NewUnits()[i];
    if (pUnit == 0)
      break;
    pUnit->sub_422EB0(i);
  }
  if (IsTeamGame()) {
    int nLocalTeam = gPlayerData[g_LocalHumanID].nTeam;
    CUnit **pp = SelectedRow(SELECTION_HUMANS);
    BYTE *pbTeam = &gPlayerData[SELECTION_HUMANS].nTeam;
    int p = SELECTION_HUMANS;
    do {
      p--;
      pp -= SelectionMax();
      pbTeam -= sizeof(PlayerData);
      if (p == (int)g_LocalHumanID)
        continue;
      if (*pbTeam != nLocalTeam)
        continue;
      int j = 0;
      CUnit **ppWalk = pp;
      for (; j < SelectionMax(); j++, ppWalk++) {
        CUnit *pUnit = *ppWalk;
        if (pUnit == 0)
          break;
        pUnit->CreateTeamCircle();
      }
    } while (pbTeam != &gPlayerData[0].nTeam);
  }
}

// Kills every image, allocates the new main
//           image (prepending it to an existing list or seeding one), re-applies
//           selection, healthbar and team-circle state, then SetFacing()s the rebuilt list.
//           The team-circle loop is AllocCircle's shape.
void CSprite::ChangeImage(int nImageID, int nDirection) {
  BYTE bFlags = this->flags.b;
  DWORD bSelected = (bFlags >> 3) & 1;
  DWORD bHasCircle = bFlags & 1;
  int nTTL = (bFlags >> 1) & 3;

  BYTE bSelColor;
  if (bSelected != 0)
    bSelColor = this->Deselect();
  else
    bSelColor = this->RemoveCircle();
  BYTE bTeamColor = this->RemoveTeamCircleIf();

  CImage **ppHead = &this->pImageHead;

  for (CImage *pImage = this->pImageHead; pImage != 0; ) {
    CImage *pNext = pImage->pNext;
    pImage->Kill();
    pImage = pNext;
  }

  CImage *pNewImage = CImage::AllocImage();
  if (pNewImage != 0) {
    if (this->pImageHead) {
      reinterpret_cast<CLink *>(this->pMainImage)->PrependLink(
          reinterpret_cast<CLink *>(pNewImage),
          reinterpret_cast<CLink **>(&this->pImageHead));
    } else {
      this->pMainImage = pNewImage;
      reinterpret_cast<CLink *>(pNewImage)->PrependThis(
          reinterpret_cast<CLink **>(&this->pImageHead),
          reinterpret_cast<CLink **>(&this->pImageTail));
    }
    pNewImage->Initialize(nImageID, this, 0, 0);
    pNewImage->InitScript();
    pNewImage->FaceDirection(0);
  }

  if (bHasCircle != 0) {
    if (bSelected != 0) {
      if (!(this->flags.b & 8)) {
        this->flags.b |= 8;
        CImage::AllocHealthbar(this);
        if (!(this->flags.b & 1)) {
          if (CImage::AllocSelect(this, bSelColor, 0x231))
            this->flags.b |= 1;
        }
      }
    } else {
      if (!(this->flags.b & 1)) {
        if (CImage::AllocSelect(this, bSelColor, 0x231))
          this->flags.b |= 1;
      }
    }
  }

  if (nTTL != 0) {
    do {
      nTTL--;
      int nTTLNow = this->flags.bits.nTeamCircleTTL;
      if (nTTLNow == GetMaxPlayersPerTeam() - 1)
        continue;
      this->flags.bits.nTeamCircleTTL++;
      if (this->flags.bits.nTeamCircleTTL > 1)
        continue;
      if (!CImage::AllocSelect(this, bTeamColor, 0x23B))
        this->flags.bits.nTeamCircleTTL = 0;
    } while (nTTL != 0);
  }

  for (CImage *pImage = *ppHead; pImage != 0; pImage = pImage->pNext)
    pImage->SetFacing(nDirection);
}

// Writes the sprite pool (the used count, then the used entries compressed) and both
// y-axis lists as pool indices, restoring the pointers after each write.
BOOL CSprite::SaveSprites(void *pFile) {
  EncodeListForSave();
  DWORD nUsed = 0;
  for (int i = 0; i < g_pools.sprites; i++)
    if (sgSprites[i].pImageHead != 0)
      nUsed++;
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(g_pools.sprites);
  BOOL bOk = pfwrite(&nUsed, nSlotBytes, pFile);
  if (bOk != 0 && nUsed != 0) {
    DWORD nBufSize = nUsed * (nSlotBytes + PortSave117Size(SAVE117_CSprite));  // Hyperion
    BYTE *pBuf = (BYTE *)SMemAlloc(nBufSize, "CSprite.cpp", 1827, 0);
    BYTE *p = pBuf;
    memset(pBuf, 0, nBufSize);
    nUsed = 0;
    for (int i = 0; i < g_pools.sprites; ++i) {
      CSprite *pSprite = &sgSprites[i];
      if (pSprite->pImageHead) {
        PoolSaveSlotPut(p, static_cast<unsigned>(i), nSlotBytes);
        p += nSlotBytes;
        voice_squelch(pSprite);
        p = PortSave117Put(SAVE117_CSprite, pSprite, p);
        voice_squelch(pSprite);
        ++nUsed;
      }
    }
    bOk = PortSaveChunk(pBuf, nBufSize, nUsed, pFile);
    SMemFree(pBuf, "CSprite.cpp", 1849, 0);
  }
  DecodeListFromSave();
  if (bOk != 0) {
    for (DWORD i = gwMapHeight; i != 0; ) {
      CSprite *pSprite = g_spriteRowHead[i - 1];
      i--;
      g_spriteRowHead[i] = reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(pSprite));
    }
    DWORD nSavedRows = gwMapHeight > 256 ? gwMapHeight : 256;  // Hyperion
    BOOL bHead = PortSaveWriteIds(pFile, g_spriteRowHead, nSavedRows);
    for (DWORD i = gwMapHeight; i != 0; ) {
      CSprite *pSprite = g_spriteRowHead[i - 1];
      i--;
      g_spriteRowHead[i] = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(pSprite));
    }
    bOk = bHead;
    if (bOk != 0) {
      for (DWORD i = gwMapHeight; i != 0; ) {
        CSprite *pSprite = g_spriteRowTail[i - 1];
        i--;
        g_spriteRowTail[i] = reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(pSprite));
      }
      BOOL bTail = PortSaveWriteIds(pFile, g_spriteRowTail, nSavedRows);
      for (DWORD i = gwMapHeight; i != 0; ) {
        CSprite *pSprite = g_spriteRowTail[i - 1];
        i--;
        g_spriteRowTail[i] = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(pSprite));
      }
      bOk = bTail;
    }
  }
  return bOk;
}
