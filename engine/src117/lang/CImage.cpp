// CImage.cpp -- a sprite's image: the four image pools and their free lists, the
// palette/RLE draw state (cloak, decloak, warp, selection circles, health bars, placement
// boxes), the frame, offset and facing setters, the rle_draw_* dispatch thunks, the iscript
// interpreter, the save codecs, and the images.dat, GRP and .lo loaders. A few functions
// 1.17 keeps in this file belong to other classes and follow in a trailing block.
//
// CImage's members are in src117/h/CImage.h's declaration order rather than the classic
// game's order.
#include "oc_data.h"  // Hyperion: OC_VA
#include "../port/data/port_formats.h"  // Hyperion
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../gen/save117.h"
#include "CImage.h"
#include "grp.h"            // ours: GROUP/FRAME bodies -- GetJuiceFrame needs GROUP complete
#include "CSprite.h"        // ours: CSprite::AllocImageAbove, for CreateOverlay
#include "CUnit.h"          // ours: pSprite @+0x0C, subunit @+0x70, wType @+0x64
#include "CLink.h"          // ours: CLink::Unlink/PrependThis/PrependLink/AppendLink
#include "unittypes.h"      // ours: UNI_T_GOLIATH / UNI_T_SCHEZAR
#include "imagetypes.h"     // ours: NUM_IMAGES, NUM_IMAGES_ORIGINAL, IMG_*
#include "grid.h"           // ours: BOOL IsBroodwar()
#include "rect32.h"         // ours: the 4-long rect CImage::GetDimensions fills
#include "bmp.h"            // ours: bmp_t -- rle_draw_texture builds one on its stack
#include "CUnitColor.h"     // ours: get_active_unit_color, CALLED by rle_draw_playerside
#include "gamesnd.h"        // ours: snd_pos_t + gamesnd_play_mtx, for CImage::PlaySnd
#include "CThingy.h"        // ours: CThingy::AllocThingy + UpdateVisibility
#include "group.h"          // ours: group_load_tbl / group_load_lo, for LoadGroups
#include "glues.h"          // ours: mem_free and (via dlg_str.h) str_load_table
#include "gds_local.h"      // ours: psnprintf, for InitImages
#include "saveload.h"       // ours: pfread / pfwrite / compress / decompress
#include "Storm.h"          // ours: SMemAlloc / SMemFree
#include "CBullet.h"         // ours: CBullet::Damage, for RunScript
#include "CFlingy.h"         // ours: AllocFlingy / CTurn / CCTurn / SetSpawnFacing
#include "CTarget.h"         // ours: CTarget::XYInRadius
#include "collide.h"         // ours: mtx_passable
#include "CUnitCombat.h"     // ours: get_angle
#include "TrigAct.h"         // ours: net_rand
#include <intrin.h>          // _ReadWriteBarrier -- CImage::RunScript

// scripts\iscript.bin's header: a DWORD offset to the id -> script table, then the script
// byte pool, which is the `data` at +4.
struct script_head_t {
  DWORD offset;
  BYTE data[1];
};

// Declared here rather than through <string.h>, so this TU's include closure stays inside
// src117/h. /Oi expands it inline, which rle_draw_shift needs instead of a call.
extern "C" void *__cdecl memcpy(void *, const void *, size_t);
// The same for memset: /Oi expands a constant-size 8-byte memset to one SSE store, which
// is the lowering the 8-byte zero stores in SelectInit and Initialize want.
extern "C" void *__cdecl memset(void *, int, size_t);

// light_g -- light_t[7] @0x50883C, Blizzard's light.cpp .data, spelled for either build.
// The array base is 0x50883C, not the 0x508840 the instruction stream suggests: 0x508840
// is &light_g[0].pData, the second field, and at the 0x14 stride the seventh element ends
// at 0x5088C7, exactly where the next .data string begins. Each element's pData points
// 0x100 bytes before its first table (sub_466810 allocates (height + 1) * 256 and decodes
// into pData + 0x100), which is what makes `pData + (level << 8)` a table selector.

#ifdef OC_DLL_MODE
#ifndef light_g
#define light_g (reinterpret_cast<light_t *>(OC_VA(0x50883Cu)))
#endif
#else
extern light_t light_g[7];
#endif

// The image free list is a head and a tail, images_sgpFreeHead_g @0x544694 and
// images_sgpFreeTail @0x592898, with the pool's 5000 CImage entries running between them.
// The hpbar, circle, placebox and placemask pools each have the same triple. Both are
// typed CImage * rather than CLink *: the prev/next pair sits at offset 0, so the casts at
// the call sites are identity conversions and cost nothing.

// The game's globals at their fixed 1.17 addresses. This include has to come after the
// game headers and after every type a global's declaration names (script_head_t above).
#if !defined(OC_DLL_MODE) && !defined(OC_PAYLOAD)
// Static in this build: every reader of 0x54330C is in this TU, and RunScript's call case
// needs the value to survive the wUnk04 store.
struct script_head_t;
static script_head_t *sgpScripts;
#endif
// Hyperion: 1.17's iscript.bin's size, where a mod's scripts start, and the scripts'
// size with a mod's; a mod's script's header (port_moddata.cpp)
static DWORD s_dwScripts117;
static DWORD s_dwScripts;
bool ModScriptHeader(int id, DWORD *pdwHeader);

// Hyperion: a DWORD of a mod's script, which need not be aligned
static inline DWORD ScriptDword(const BYTE *p)
{
  DWORD d;
  memcpy(&d, p, 4);
  return d;
}
#include "../gen/globals117.h"
#include "../h/gamedata.h"  // Hyperion: array_data_t
#include "../h/WeaponGndEx.h"   // CUnit::GetWeaponGndEx's inline body, for RunScript
#include "../h/typelimits.h"  // Hyperion: a mod's types
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// Hyperion: a mod's image whose sheet gives 32 directions, none mirrored, where
// classic's turning images have 17 and mirror the other 15 (port_moddata.cpp sets
// it as the mod's data loads). Its frames are 32 to a row, and its script's frame sets still
// count 17 to a row, as every image's do, so one script serves a sheet of either.
BYTE g_hxModImage32[IMAGE_TYPES_MAX - IMAGE_TYPES_117];
static inline bool Image32(const CImage *pImage)
{
  return pImage->wImageID >= IMAGE_TYPES_117 && g_hxModImage32[pImage->wImageID - IMAGE_TYPES_117];
}
// A direction of 32 as the image holds it: classic's upper half mirrored, or a 32's own.
static inline BYTE ImageFold(const CImage *pImage, BYTE bDir, BYTE *pbFlip)
{
  if (Image32(pImage)) {
    *pbFlip = 0;
    return (BYTE)(bDir & 0x1F);
  }
  if (bDir > 0x10) {
    *pbFlip = 1;
    return (BYTE)(0x20 - bDir);
  }
  *pbFlip = 0;
  return bDir;
}
// The frame shown for frame set nSet in direction nDir: a 32's rows are 32 frames.
static inline int ImageFrame(const CImage *pImage, int nSet, int nDir)
{
  return Image32(pImage) ? nSet / 17 * 32 + nSet % 17 + nDir : nSet + nDir;
}
// The frame sets an image's group holds, counted 17 to a row as a script's sets are.
static inline int ImageSets(const CImage *pImage)
{
  const int nFrames = *reinterpret_cast<const WORD *>(pImage->pGroup) & 0x7FFF;
  return Image32(pImage) ? (nFrames + 31) / 32 * 17 : nFrames;
}
// pImage takes pFrom's direction and mirroring, as classic copies them; between a 17 and a 32
// the direction is turned from the one's to the other's.
static void ImageTakeDirection(CImage *pImage, const CImage *pFrom)
{
  if (Image32(pImage) == Image32(pFrom)) {
    pImage->bDirection = pFrom->bDirection;
    pImage->flags.b.Mirror = pFrom->flags.b.Mirror;
    return;
  }
  BYTE bFlip;
  pImage->bDirection = ImageFold(pImage, (BYTE)(pFrom->flags.b.Mirror ? 0x20 - pFrom->bDirection : pFrom->bDirection),
                                 &bFlip);
  pImage->flags.b.Mirror = bFlip;
}

// sgImages -- the main image pool, CImage[5000] @0x544698, spelled for either build.
#ifdef OC_DLL_MODE
#ifndef sgImages
#define sgImages (reinterpret_cast<CImage *>(OC_VA(0x544698u)))
#endif
#else
extern CImage sgImages[POOL_IMAGES_STORE];
#endif

// 0x50882C is not a global: it is light_g[-1].pData, a folded 1-based base. Decode indexes
// it with the 1-based light id and does not range-check that id, so id 0 reads the .data
// dword before the table, and the source reproduces that read rather than fixing it.
// Hyperion: the view is gone. With the globals ours (step 6) it was an
// array of its own, all zero, so no light matched and every palette-9 image
// saved 0, which a load reads as light_g[-1]. Encode walks light_g itself
//.

// ECX is the index, not `this`: /Gr makes
//            this static member __fastcall. sgpImgGroups is the per-image-type GRP
//            pointer table.
PGROUP CImage::GetImageFromGroupIndex(ULONG id) {
  return sgpImgGroups[id];
}

// Set both offsets, dirty on change.
void CImage::SetOffset(char x, char y) {
  if (this->cHorzOffset == x && this->cVertOffset == y) return;
  this->flags.b.Dirty = 1;
  this->cHorzOffset = x;
  this->cVertOffset = y;
}

// Inlines SetOffset(cHorzOffset, y): only the
//            cVertOffset arm survives, so this is the vertical-only dirty-set.
void CImage::SetVerticalOffset(char y) {
  SetOffset(this->cHorzOffset, y);
}

// Unhide: clear Hidden, request
//            a redraw.
void CImage::Show() {
  if (this->flags.b.Hidden) {
    this->flags.b.Hidden = 0;
    this->flags.b.Dirty = 1;
  }
}

// Reset this image to its
//            images.dat default iscript. ForceScript takes the id by value.
void CImage::SetScriptDefault() {
  this->ForceScript(img_iscriptId[this->wImageID]);
}

// Store the player/color selector into the
//            colouring cell's dword arm at CImage+0x30.
void CImage::SetRLEPlayer(BYTE bPlayer) {
  this->coloringData.dw = bPlayer;
}

// Hide: invalidate the current
//            rect (only if not already hidden), then set Hidden.
void CImage::Hide() {
  if (this->flags.b.Hidden) return;
  this->Invalidate();
  this->flags.b.Hidden = 1;
}

// Pop the front of the global free-image list
//            and hand it to the caller, or return null when the pool is exhausted.
//            The node is handed back uncleared, since CLink::Unlink nulls only its
//            own two links, and CImage::Initialize overwrites the rest -- every
//            caller runs it immediately.
CImage * __fastcall CImage::AllocImage()
{
  CImage *pImage = images_sgpFreeHead_g;
  if (pImage == 0)
    return 0;

  reinterpret_cast<CLink *>(pImage)->Unlink(
      reinterpret_cast<CLink **>(&images_sgpFreeHead_g),
      reinterpret_cast<CLink **>(&images_sgpFreeTail));

  return pImage;
}

// The GRP frame actually drawn is the
//            animation's frame set plus the facing direction within it, so this
//            recomputes it and, only if it moved, records the new one and marks
//            the image for redraw. The comparison is 32-bit and the store
//            truncates, so the local is `int` and the narrowing a separate step
//            at the store.
void CImage::CalcDisplayNo()
{
  int nDisplayNo = ImageFrame(this, this->wFrameSet, this->bDirection);  // Hyperion: a 32's

  if (this->wFrameIndex != nDisplayNo) {
    this->flags.b.Dirty = 1;
    this->wFrameIndex = (WORD)nDisplayNo;
  }
}

// Put this image into the fully-cloaked draw
//            state: palette type 2, whose sgDrawFuncs row is rle_draw_invis /
//            rle_draw_invis_hflip. Every absolute address in the body is
//            &table[2].field, not a global of its own.
void CImage::CloakInit()
{
  this->bPaletteType = 2;
  this->pfnUpdate = sgUpdateFuncs[2].pfnUpdate;

  this->pfnRender = (this->flags.w & 2) ? sgDrawFuncs[2].pfnFlipped
                                        : sgDrawFuncs[2].pfnNormal;

  this->flags.b.Dirty = 1;
  this->coloringData.rle.bRleFrame = 0;
  this->coloringData.rle.bRleWait = 3;
}

// Switch this image to a different animation
//            frame set, and if that moved it, recompute the GRP frame being drawn.
//            The comparison is 32-bit against the unnarrowed argument, so the
//            parameter is `int`, not WORD: SetFrame(0x10000) falls through to a
//            store that truncates to 0 -- 1.17's asymmetry, unreachable in
//            practice. wFrameSet is stored before CalcDisplayNo runs.
void CImage::SetFrame(int nFrameSet)
{
  if (this->wFrameSet == nFrameSet)
    return;

  this->wFrameSet = (WORD)nFrameSet;
  this->CalcDisplayNo();
}

// Play a sound positioned at this image's
//            owning sprite. The point is a 4-byte object passed by value;
//            src117/h/gamesnd.h has the type and the callee's convention. Both
//            coordinates are read unsigned, wPosY included, though CSprite.h
//            types it signed: only the low half is live.
void CImage::PlaySnd(int nSound)
{
  CSprite *pSprite = this->pParentSprite;

  snd_pos_t pos;
  pos.x = pSprite->wPosX;
  pos.y = pSprite->wPosY;

  gamesnd_play_mtx(nSound, 1, pos, 0);
}

// Point an image at a palette/RLE type: record it, take that type's update and draw
// procedures, apply the 0x11 special case, and mark the image dirty. Flags are updated
// on the member, not through a local copy.
void CImage::SetRLE(BYTE bRLE)
{
  // Hyperion: the tables' 18 functions; a mod's images.dat may name any byte
  if (bRLE >= 18)
    bRLE = 0;
  this->bPaletteType = bRLE;
  this->pfnUpdate    = sgUpdateFuncs[bRLE].pfnUpdate;

  if (this->flags.w & 2)
    this->pfnRender = sgDrawFuncs[bRLE].pfnFlipped;
  else
    this->pfnRender = sgDrawFuncs[bRLE].pfnNormal;

  if (bRLE == 0x11) {
    this->coloringData.rle.bRleFrame = 0x30;
    this->coloringData.rle.bRleWait  = 0x02;
  }

  this->flags.w |= 1;
}

// CImage::RemoveHPBar (below) with the
//            circle pool's free-list head and tail. It is defined above
//            RemoveHPBar because src117/h/CImage.h declares it first.
void CImage::RemoveCircle()
{
  this->Invalidate();

  CSprite *pSprite = this->pParentSprite;
  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&pSprite->pImageHead),
      reinterpret_cast<CLink **>(&pSprite->pImageTail));

  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&circle_images_sgpFreeHead_g),
      reinterpret_cast<CLink **>(&circle_images_sgpFreeTail_g));
}

// Take an image off the selection-circle pool
//            and splice it in relative to the sprite's last image, then initialise
//            it. The object of both list calls is the tail image, loaded once
//            before the test: PrependLink with &pImageHead, AppendLink with
//            &pImageTail, as src117/h/CLink.h's two signatures require. 0x0A stays
//            a literal: an image already of that palette type stays in front.
CImage * __fastcall CImage::AllocSelect(CSprite *pSprite, BYTE bSelIndex,
                                        int nImageID)
{
  CImage *pImage = circle_images_sgpFreeHead_g;
  if (pImage == 0)
    return 0;

  reinterpret_cast<CLink *>(pImage)->Unlink(
      reinterpret_cast<CLink **>(&circle_images_sgpFreeHead_g),
      reinterpret_cast<CLink **>(&circle_images_sgpFreeTail_g));

  CImage *pTail = pSprite->pImageTail;
  if (pTail->bPaletteType == 0x0A)
    reinterpret_cast<CLink *>(pTail)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&pSprite->pImageHead));
  else
    reinterpret_cast<CLink *>(pTail)->AppendLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&pSprite->pImageTail));

  pImage->SelectInit(pSprite, bSelIndex, nImageID);
  return pImage;
}

// The inverse of CloakInit: move the image
//            into whichever decloaking draw state matches the one it is leaving.
//            Palettes 2 (invis) and 5 (cloak) are the two cloaked states, so
//            `< 4 ? 4 : 7` is "decloak from fully hidden" against "decloak from
//            partial". The 0x230 store is dead, as in 1.17: the guard sees 4 or
//            7, never 0x11, and the 0x308 store overwrites it.
void CImage::DecloakInit()
{
  BYTE bPaletteType = (this->bPaletteType < 4) ? 4 : 7;
  this->bPaletteType = bPaletteType;

  this->pfnUpdate = sgUpdateFuncs[bPaletteType].pfnUpdate;

  this->pfnRender = (this->flags.w & 2) ? sgDrawFuncs[bPaletteType].pfnFlipped
                                        : sgDrawFuncs[bPaletteType].pfnNormal;

  if (bPaletteType == 0x11) {          // 1.17's dead arm: unreachable, and overwritten
    this->coloringData.rle.bRleFrame = 0x30;
    this->coloringData.rle.bRleWait = 0x02;
  }

  this->flags.b.Dirty = 1;
  this->coloringData.rle.bRleFrame = 0x08;
  this->coloringData.rle.bRleWait = 0x03;
}

// Return a health-bar image to its own free
//            pool: mark the screen rect dirty, take the image out of its owning
//            sprite's image list, then push it onto the front of the hpbar free
//            list. Invalidate comes first, since it reads this image's own
//            screen rect.
void CImage::RemoveHPBar()
{
  this->Invalidate();

  CSprite *pSprite = this->pParentSprite;
  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&pSprite->pImageHead),
      reinterpret_cast<CLink **>(&pSprite->pImageTail));

  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&hpbar_images_sgpFreeHead),
      reinterpret_cast<CLink **>(&hpbar_images_sgpFreeTail_g));
}

// Retire an image: repaint the rect it
//            occupied, take it out of its owning sprite's image list, drop its
//            GRP, and push it back onto the front of the global free list. The
//            rest of its state is left for CImage::Initialize to clear.
void CImage::Kill()
{
  this->Invalidate();

  reinterpret_cast<CLink *>(this)->Unlink(
      reinterpret_cast<CLink **>(&this->pParentSprite->pImageHead),
      reinterpret_cast<CLink **>(&this->pParentSprite->pImageTail));

  this->pGroup = 0;

  reinterpret_cast<CLink *>(this)->PrependThis(
      reinterpret_cast<CLink **>(&images_sgpFreeHead_g),
      reinterpret_cast<CLink **>(&images_sgpFreeTail));
}

// Fill the caller's rect with the screen box
//            this image's current GRP frame occupies. The rect is inclusive:
//            right and bottom name the last covered pixel, hence the `- 1`.
void CImage::GetDimensions(rect32_t *prc)
{
  FRAME *pFrame = &this->pGroup->frames[this->wFrameIndex];

  prc->left   = this->wMapX;
  prc->top    = this->wMapY;
  prc->right  = this->wMapX - 1 + pFrame->bWidth;
  prc->bottom = this->wMapY - 1 + pFrame->bHeight;
}

// Take an image off the healthbar pool, put
//            it at the front of the sprite's image list, and initialise it.
//            Static and __fastcall, with the CSprite in ECX as an argument.
//            Nothing guards a null pImageHead; 1.17 relies on a healthbar only
//            ever being added to a sprite that already has a main image.
CImage * __fastcall CImage::AllocHealthbar(CSprite *pSprite)
{
  CImage *pImage = hpbar_images_sgpFreeHead;
  if (pImage == 0)
    return 0;

  reinterpret_cast<CLink *>(pImage)->Unlink(
      reinterpret_cast<CLink **>(&hpbar_images_sgpFreeHead),
      reinterpret_cast<CLink **>(&hpbar_images_sgpFreeTail_g));

  reinterpret_cast<CLink *>(pSprite->pImageHead)->PrependLink(
      reinterpret_cast<CLink *>(pImage),
      reinterpret_cast<CLink **>(&pSprite->pImageHead));

  pImage->HealthbarInit(pSprite);
  return pImage;
}

// images.dat entry count: 999 in
//            Brood War, 755 in vanilla.
int GetNumImages() {
  return IsBroodwar() ? NUM_IMAGES : NUM_IMAGES_ORIGINAL;
}

// The frames of sgpImgGroups[584]
//            (IMG_PYLON_POWER_TOP), the index folded into the address.
PFRAME GetJuiceFrame() {
  return sgpImgGroups[IMG_PYLON_POWER_TOP]->frames;
}

iscript_anim_hdr_t *get_image_script(int offset) {
  return (iscript_anim_hdr_t*)&sgpScripts->data[offset];
}

// The id -> header table the iscript.bin header's `offset` member points at: a flat array
// of {id, headerOffset} WORD pairs terminated by id == 0xFFFF.
struct script_id_ent_t {
  WORD wScriptId;      /*0x00*/
  WORD wHeaderOffset;  /*0x02*/
};
static_assert(sizeof(script_id_ent_t) == 4, "script_id_ent_t stride");
static_assert(offsetof(script_id_ent_t, wScriptId) == 0, "wScriptId");
static_assert(offsetof(script_id_ent_t, wHeaderOffset) == 2, "wHeaderOffset");

// appcritf -- oracle 0x4CC190, the same __cdecl varargs spelling src117/lang/dlg_draw.cpp
// and src117/lang/init.cpp use.
void __cdecl appcritf(const char *pszFmt, ...);

// The table base is
//            `(BYTE *)sgpScripts + sgpScripts->offset`, the header's own offset
//            member added to the header's address rather than to ->data. The
//            0xFFFF terminator check is inside the loop and after the hit test,
//            so a first-iteration hit skips it.
void CImage::ForceScript(int id) {
  // Hyperion: a mod's script, its id from 1000, from a table of its own whose offsets
  // pass a word (port_moddata.cpp)
  DWORD dwHeader;
  if (id >= 1000 && ModScriptHeader(id, &dwHeader)) {
    this->wIscriptHeader = static_cast<iscript_off_t>(dwHeader);
    return;
  }

  const script_id_ent_t *pEnt = reinterpret_cast<const script_id_ent_t *>(
      reinterpret_cast<BYTE *>(sgpScripts) + sgpScripts->offset);

  while (pEnt->wScriptId != id) {
    if (pEnt->wScriptId == 0xFFFF)
      appcritf("script %d does not exist", id);
    ++pEnt;
  }

  this->wIscriptHeader = pEnt->wHeaderOffset;
}

// The rle_draw_* dispatch thunks. The family's calling convention (`__fastcall (r1, r2,
// a3, a4, a5)`, `ret 0Ch`), the four blitters they forward to and the dead fifth parameter
// are described with the declarations in src117/h/CImage.h. Every thunk that is not a pure
// argument rewrite makes two calls, and both legs take the same five arguments.

// Two legs: the second takes the caller's own
//            fifth argument through unchanged while the first gets a constant
//            zero, the slot rle_draw_cloak below fills from a global.
void __fastcall rle_draw_invis(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwArg)
{
  rle_draw_invis_asm(nX, nY, pGraphic, pDraw, 0);
  rle_draw_uncloaked_part(nX, nY, pGraphic, pDraw, dwArg);
}

// The hflip twin: both legs swap to their
//            _hflip counterparts and nothing else changes.
void __fastcall rle_draw_invis_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwArg)
{
  rle_draw_invis_hflip_asm(nX, nY, pGraphic, pDraw, 0);
  rle_draw_uncloaked_part_hflip(nX, nY, pGraphic, pDraw, dwArg);
}

// rle_draw_invis' shape with the constant
//            replaced by a read of the cloak remap table, and a different first
//            leg.
void __fastcall rle_draw_cloak(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwArg)
{
  rle_draw_effect(nX, nY, pGraphic, pDraw, dword_508890);
  rle_draw_uncloaked_part(nX, nY, pGraphic, pDraw, dwArg);
}

// The hflip twin of the pair above.
void __fastcall rle_draw_cloak_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwArg)
{
  rle_draw_effect_hflip(nX, nY, pGraphic, pDraw, dword_508890);
  rle_draw_uncloaked_part_hflip(nX, nY, pGraphic, pDraw, dwArg);
}

// Swap the shared remap table over to a given
//            player colour, blit, then put the old colour back.
//
//            The fifth parameter is a BYTE in a dword slot. sub_41D740's is an
//            int, even though that callee opens by narrowing its own ECX.
void __fastcall rle_draw_playerside(int nX, int nY, void *pGraphic,
                                    void *pDraw, BYTE bColor)
{
  BYTE bSaved = (BYTE)get_active_unit_color();

  sub_41D740(bColor);
  rle_draw_normal(nX, nY, pGraphic, pDraw, 0);
  sub_41D740(bSaved);
}

// rle_draw_playerside with the hflip blitter.
void __fastcall rle_draw_playerside_hflip(int nX, int nY, void *pGraphic,
                                          void *pDraw, BYTE bColor)
{
  BYTE bSaved = (BYTE)get_active_unit_color();

  sub_41D740(bColor);
  rle_draw_hflip_(nX, nY, pGraphic, pDraw, 0);
  sub_41D740(bSaved);
}

// The selection-circle blit: patch entries
//            [1] through [8] of the shared remap table with the two dwords of
//            this selection type's palette row, blit, then put the old eight
//            back. The saved pair lives in two DWORD locals. sgSelectPalette_g is
//            indexed flat: it is 3 rows of 2 dwords, so row n is [n*2] and
//            [n*2+1].
void __fastcall rle_draw_select(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE bSelectType)
{
  DWORD dwSavedLo = *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[1]);
  DWORD dwSavedHi = *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[5]);

  *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[1]) = sgSelectPalette_g[bSelectType * 2];
  *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[5]) = sgSelectPalette_g[bSelectType * 2 + 1];

  rle_draw_normal(nX, nY, pGraphic, pDraw, 0);

  *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[1]) = dwSavedLo;
  *reinterpret_cast<DWORD *>(&_gbRemapColorTbl[5]) = dwSavedHi;
}

// The whole remap table, not eight bytes of
//            it: stash all 256 entries on the stack, install the requested shift
//            row, blit, restore. The fifth argument is a DWORD row index, not a
//            BYTE. The indexed read is spelled as pointer arithmetic because a
//            second row would overlap gWarpTextureFrame_g @0x59C2B0, which is its
//            own object.
void __fastcall rle_draw_shift(int nX, int nY, void *pGraphic, void *pDraw,
                               DWORD dwShift)
{
  BYTE bSaved[256];

  memcpy(bSaved, _gbRemapColorTbl, 256);
  memcpy(_gbRemapColorTbl, sgShiftImg_g + (dwShift << 8), 256);

  rle_draw_normal(nX, nY, pGraphic, pDraw, 0);

  memcpy(_gbRemapColorTbl, bSaved, 256);
}

// rle_draw_shift with the hflip blitter.
void __fastcall rle_draw_shift_hflip(int nX, int nY, void *pGraphic,
                                     void *pDraw, DWORD dwShift)
{
  BYTE bSaved[256];

  memcpy(bSaved, _gbRemapColorTbl, 256);
  memcpy(_gbRemapColorTbl, sgShiftImg_g + (dwShift << 8), 256);

  rle_draw_hflip_(nX, nY, pGraphic, pDraw, 0);

  memcpy(_gbRemapColorTbl, bSaved, 256);
}

// Two blits, and not the same one twice.
//            First the warp texture is drawn off-screen into gWarpTextureFrame_g
//            by pointing _bmp_current at a bmp_t on this function's stack; then
//            the real destination is restored and the rendered buffer is the
//            remap table of a mask blit at the caller's coordinates. The
//            temporary surface's extent is the clip block's +8 and +0x0C, read
//            16 bits wide.
void __fastcall rle_draw_texture(int nX, int nY, void *pGraphic, void *pDraw,
                                 DWORD dwFrame)
{
  bmp_t bmpTexture;

  void *pSavedBmp = _bmp_current;

  bmpTexture.nWidth  = reinterpret_cast<const WORD *>(pDraw)[4];   // pDraw + 0x08
  bmpTexture.nHeight = reinterpret_cast<const WORD *>(pDraw)[6];   // pDraw + 0x0C
  bmpTexture.pPixels = gWarpTextureFrame_g;
  _bmp_current = &bmpTexture;

  rle_draw_normal(0, 0, &sgpImgGroups[IMG_P_WARP_TEXTURE]->frames[dwFrame], pDraw, 0);

  _bmp_current = pSavedBmp;

  rle_mask_warp_flash(nX, nY, pGraphic, pDraw, gWarpTextureFrame_g);
}

// The hflip twin mirrors only the mask: its
//            first call is still plain rle_draw_normal and only the second
//            becomes rle_mask_warp_flash_hflip. The texture is rendered into a
//            private buffer whose orientation does not matter, and the mirror is
//            applied when that buffer is composited onto the screen.
void __fastcall rle_draw_texture_hflip(int nX, int nY, void *pGraphic,
                                       void *pDraw, DWORD dwFrame)
{
  bmp_t bmpTexture;

  void *pSavedBmp = _bmp_current;

  bmpTexture.nWidth  = reinterpret_cast<const WORD *>(pDraw)[4];   // pDraw + 0x08
  bmpTexture.nHeight = reinterpret_cast<const WORD *>(pDraw)[6];   // pDraw + 0x0C
  bmpTexture.pPixels = gWarpTextureFrame_g;
  _bmp_current = &bmpTexture;

  rle_draw_normal(0, 0, &sgpImgGroups[IMG_P_WARP_TEXTURE]->frames[dwFrame], pDraw, 0);

  _bmp_current = pSavedBmp;

  rle_mask_warp_flash_hflip(nX, nY, pGraphic, pDraw, gWarpTextureFrame_g);
}

// A forwarder that substitutes the cloak
//            remap table for the caller's own fifth argument, which it drops.
//            The dead parameter is still declared, since the caller passes it
//            (src117/h/CImage.h). Like 1.17, it calls rle_draw_effect rather
//            than tail-jumping into it.
void __fastcall rle_draw_cloaked(int nX, int nY, void *pGraphic, void *pDraw,
                                 DWORD dwCloakStage)
{
  rle_draw_effect(nX, nY, pGraphic, pDraw, dword_508890);
}

// rle_draw_cloaked with the hflip callee.
void __fastcall rle_draw_cloaked_hflip(int nX, int nY, void *pGraphic,
                                       void *pDraw, DWORD dwCloakStage)
{
  rle_draw_effect_hflip(nX, nY, pGraphic, pDraw, dword_508890);
}

// Turn the caller's warp level into the
//            address of that level's 256-byte row in the "ofire" light table,
//            then hand the whole frame to the real blitter. It is a tail call,
//            legal because the two argument lists occupy the same stack and
//            registers.
void __fastcall rle_draw_warp_fade(int x, int y, FRAME *pFrame,
                                   rle_clip_t *pClip, BYTE bLevel)
{
  rle_draw_fade(x, y, pFrame, pClip, light_g[0].pData + (bLevel << 8));
}

// The horizontally-flipped arm of the pair
//            above: the flip lives entirely in the callee, rle_draw_fade_hflip,
//            which takes the same arguments in the same slots.
void __fastcall rle_draw_warp_fade_hflip(int x, int y, FRAME *pFrame,
                                         rle_clip_t *pClip, BYTE bLevel)
{
  rle_draw_fade_hflip(x, y, pFrame, pClip, light_g[0].pData + (bLevel << 8));
}

// 0x40FD10 sits inside the CImage address cluster (0x40EF50-0x411820), between
// CImage::Hide and CImage::SetVerticalOffset, and its only caller is CImage::RunScript, so
// this TU is its home even though it is a CUnit member.

// Goliath body or Alan Schezar (the hero
//            Goliath), the same {unit, hero} pairing every other Is*Type
//            predicate uses. The two turret types (GOLIATH_HEAD 0x04,
//            SCHEZAR_HEAD 0x12) are deliberately not here.
BOOL CUnit::IsGoliathType() const {
  return this->wType == UNI_T_GOLIATH || this->wType == UNI_T_SCHEZAR;
}

// The unit has an energy pool when its type
//            carries the units.dat column at 0x200000 and the unit itself does
//            not carry the dwFlags bit 0x40000000 that suppresses it. The masks
//            stay raw: nothing says what 0x40000000 means. The same pair in the
//            same order runs inside ai_count_collateral_energy (0x433410).
BOOL CUnit::HasEnergy()
{
  return (gulUnitTypeFlags[this->wType] & 0x200000)
      && !(this->dwFlags & 0x40000000);
}

// Functions 1.17 keeps in this file that are not CImage members.

// sub_417EF0 lives in CThingy.cpp and get_active_unit_color in CUnitColor.cpp, the TUs the
// oracle map gives them. With either body in this TU, MSVC inlined it into its caller here
// and the two playerside bodies grew.

// Attach an overlay image to the
//            unit's sprite, preferring the subunit's sprite when one exists (turret
//            overlays land on the turret, not the base).
//
//            A tail call into CSprite::AllocImageAbove, legal only while the two
//            signatures agree (src117/h/CSprite.h).
void CUnit::CreateOverlay(int imgNo, char x, char y, int nDirection) {
  CUnit *pUnit = this->subunit ? this->subunit : this;
  pUnit->pSprite->AllocImageAbove(imgNo, x, y, nDirection);
}

// 1-based pool id -> pointer; 0 is NULL.
CImage *CImage::GetImage(DWORD nImage) {
  // Hyperion: and none past the pool, which a save's id may name
  if (nImage != 0 && nImage <= static_cast<DWORD>(g_pools.images))
    return &sgImages[nImage - 1];
  return 0;
}

// The inverse: 0 for NULL, else the 1-based
//           pool id, the subtraction done in a DWORD rather than as a pointer
//           difference.
DWORD CImage::GetImageID(CImage *pImage) {
  if (pImage != 0) {
    DWORD_PTR dwOff = reinterpret_cast<DWORD_PTR>(pImage) - reinterpret_cast<DWORD_PTR>(sgImages);
    return (DWORD)(dwOff / sizeof(CImage) + 1);
  }
  return 0;
}

// Converts every file-loaded id back to
//           its pointer: the group id -> sgpImgGroups[id - 1], the palette-0x0B unit id ->
//           CUnit::FromIdAlive, the palette-9 light id -> the light-table slot, the owning
//           sprite id -> CSprite::GetSprite, and the prev/next link ids -> GetImage. Also
//           re-derives pfnRender/pfnUpdate from the palette-type tables whenever the group
//           id is nonzero.
void CImage::DecodeListFromSave() {
  CImage *p = sgImages;
  DWORD n = g_pools.images;
  while (n != 0) {
    n--;
    DWORD id = (DWORD)reinterpret_cast<DWORD_PTR>(p->pGroup);
    if (p->pGroup != 0) {
      if (p->flags.w & 2)
        p->pfnRender = sgDrawFuncs[p->bPaletteType].pfnFlipped;
      else
        p->pfnRender = sgDrawFuncs[p->bPaletteType].pfnNormal;
      p->pfnUpdate = sgUpdateFuncs[p->bPaletteType].pfnUpdate;
    }
    p->pGroup = (id != 0) ? sgpImgGroups[id - 1] : 0;

    if (p->bPaletteType == 0x0B) {
      p->coloringData.dw =
          reinterpret_cast<DWORD_PTR>(CUnit::FromIdAlive((DWORD)p->coloringData.dw));
    } else if (p->bPaletteType == 9) {
      p->coloringData.dw = reinterpret_cast<DWORD_PTR>(light_g[p->coloringData.dw - 1].pData);
    }

    p->pParentSprite = CSprite::GetSprite((int)reinterpret_cast<intptr_t>(p->pParentSprite));
    p->pPrev = CImage::GetImage((DWORD)reinterpret_cast<DWORD_PTR>(p->pPrev));
    p->pNext = CImage::GetImage((DWORD)reinterpret_cast<DWORD_PTR>(p->pNext));
    p++;
  }
}

// The save side's inverse: group pointer -> 1-based sgpImgGroups index, unit pointer ->
// CUnit::GetUnitID, light-table pointer -> slot index, owning sprite ->
// CSprite::GetSpriteID, prev/next -> GetImageID. 1.17 quirk, matched not fixed: the two
// null stores to pfnRender/pfnUpdate at the top of every entry destroy the procedure
// pointers on the save path.
void CImage::EncodeListForSave() {
  CImage *p = sgImages;
  DWORD n = g_pools.images;
  while (n != 0) {
    n--;
    GROUP *pGroup = p->pGroup;
    p->pfnRender = 0;
    p->pfnUpdate = 0;
    DWORD i = 0;
    if (p->pGroup != 0) {
      // Hyperion: and a mod's images, whose own frames are past 999;
      // g_hxTypes.images is 999 in a game without them
      for (; i < (DWORD)(IsBroodwar() ? g_hxTypes.images : 755); i++) {
        if (sgpImgGroups[i] == pGroup) {
          i++;
          goto store_group;
        }
      }
      i = 0;
    }  store_group:
    BYTE bType = p->bPaletteType;
    p->pGroup = reinterpret_cast<GROUP *>((DWORD_PTR)i);
    if (bType == 0x0B) {
      p->coloringData.dw =
          reinterpret_cast<CUnit *>(p->coloringData.dw)->GetUnitID();
    } else if (bType == 9) {
      // The 1-based light id, 0 for none; the oracle's walk also tests the
      // dword before the table, which stores 0 on a match as well.
      int iLight = 7;
      while (iLight > 0 && p->coloringData.dw != reinterpret_cast<DWORD_PTR>(light_g[iLight - 1].pData))
        iLight--;
      p->coloringData.dw = iLight;
    }

    p->pParentSprite =
        reinterpret_cast<CSprite *>((DWORD_PTR)CSprite::GetSpriteID(p->pParentSprite));
    p->pPrev = reinterpret_cast<CImage *>((DWORD_PTR)CImage::GetImageID(p->pPrev));
    p->pNext = reinterpret_cast<CImage *>((DWORD_PTR)CImage::GetImageID(p->pNext));
    p++;
  }
}

// gpImageUnit @0x543300 and gpImageTargeter @0x543304 -- the unit whose iscript is
// currently executing and the unit it is aimed at, both spelled for either build. Typed
// CUnit * from the use: rle_advance_cloak loads 0x543300 into ECX and calls
// CUnit::SetCloakStatus on it, and rle_advance_warp_fade ORs a bit into bOrderFlags.
#ifdef OC_DLL_MODE
#ifndef gpImageUnit
#define gpImageUnit (*reinterpret_cast<CUnit **>(OC_VA(0x543300u)))
#endif
#ifndef gpImageTargeter
#define gpImageTargeter (*reinterpret_cast<CUnit **>(OC_VA(0x543304u)))
#endif
#else
extern CUnit *gpImageUnit;
extern CUnit *gpImageTargeter;
#endif

void __fastcall rle_advance_decloak(CImage *pImage)
{
  BYTE bWait = pImage->coloringData.rle.bRleWait;
  if (bWait != 0) {
    pImage->coloringData.rle.bRleWait = (BYTE)(bWait - 1);
    return;
  }

  BYTE bFrame = pImage->coloringData.rle.bRleFrame;
  pImage->coloringData.rle.bRleWait  = 3;
  pImage->coloringData.rle.bRleFrame = (BYTE)(bFrame - 1);

  if (bFrame == 0) {
    pImage->bPaletteType = bFrame;
    pImage->pfnUpdate   = sgUpdateFuncs[bFrame].pfnUpdate;
    pImage->pfnRender   = (pImage->flags.w & 2)
                              ? sgDrawFuncs[bFrame].pfnFlipped
                              : sgDrawFuncs[bFrame].pfnNormal;
    pImage->flags.b.Dirty = 1;
    gpImageUnit->SetCloakStatus(0);
  }

  pImage->flags.b.Dirty = 1;
}

void __fastcall rle_advance_cloak(CImage *pImage)
{
  BYTE bWait = pImage->coloringData.rle.bRleWait;
  if (bWait != 0) {
    pImage->coloringData.rle.bRleWait = (BYTE)(bWait - 1);
    return;
  }

  pImage->coloringData.rle.bRleFrame++;
  pImage->coloringData.rle.bRleWait = 3;

  if (pImage->coloringData.rle.bRleFrame >= 8) {
    pImage->SetRLE((BYTE)(pImage->bPaletteType + 1));
    gpImageUnit->SetCloakStatus(3);
  }

  pImage->flags.b.Dirty = 1;
}

// 1.17 does not call CImage::SetAnimation here; it writes the constant-folded block out,
// which is what the pAnim initialiser below is.
void __fastcall rle_advance_warp_fade(CImage *pImage)
{
  BYTE bWait = pImage->coloringData.rle.bRleWait;
  if (bWait != 0) {
    pImage->coloringData.rle.bRleWait = (BYTE)(bWait - 1);
    return;
  }

  BYTE bFrame = pImage->coloringData.rle.bRleFrame;
  pImage->coloringData.rle.bRleWait = 2;

  if (bFrame < 0x3F) {
    pImage->coloringData.rle.bRleFrame = (BYTE)(bFrame + 1);
    pImage->flags.b.Dirty = 1;
    return;
  }

  // CImage::SetAnimation written out with bAnim = 1 folded in, which is what 1.17 emits.
  // The whole address stays one initialiser, or the two loads come out in the other order.
  if (pImage->bAnim != 1) {
    iscript_off_t *pIscript = &pImage->wIscriptHeader;
    const iscript_off_t anim = ImageScriptAnim(*pIscript, 1);  // Hyperion: and a mod's header

    pImage->bAnim = 1;
    pImage->wIscriptOffset = anim;
    pImage->bWait = 0;
    pImage->w14 = 0;
    pImage->RunScript(pIscript, 0, 0);
  }

  gpImageTargeter->bOrderFlags |= 1;
  pImage->flags.b.Dirty = 1;
}

// The placebox and placemask image pools and their free lists, each spelled for either
// build. A pool is 64 entries: CImage::InitImages memsets 0x594070 with size 0x1000 over a
// 0x3F + 1 loop and sizeof(CImage) is 0x40. The head sits one dword below each pool and the
// tail at pool + size, the same convention as the main pool above; the placemask pool is
// 0x1004 higher.
#ifdef OC_DLL_MODE
#ifndef placebox_images_sgpFreeHead_g
#define placebox_images_sgpFreeHead_g (*reinterpret_cast<CImage **>(OC_VA(0x59406Cu)))
#endif
#ifndef sgPlaceboxImages_g
#define sgPlaceboxImages_g (reinterpret_cast<CImage *>(OC_VA(0x594070u)))
#endif
#ifndef placebox_images_sgpFreeTail_g
#define placebox_images_sgpFreeTail_g (*reinterpret_cast<CImage **>(OC_VA(0x595070u)))
#endif
#ifndef placemask_images_sgpFreeHead_g
#define placemask_images_sgpFreeHead_g (*reinterpret_cast<CImage **>(OC_VA(0x595074u)))
#endif
#ifndef sgPlacemaskImages_g
#define sgPlacemaskImages_g (reinterpret_cast<CImage *>(OC_VA(0x595078u)))
#endif
#ifndef placemask_images_sgpFreeTail_g
#define placemask_images_sgpFreeTail_g (*reinterpret_cast<CImage **>(OC_VA(0x596078u)))
#endif
#else
extern CImage *placebox_images_sgpFreeHead_g;
extern CImage  sgPlaceboxImages_g[64] OC_UNALIGNED;
extern CImage *placebox_images_sgpFreeTail_g;
extern CImage *placemask_images_sgpFreeHead_g;
extern CImage  sgPlacemaskImages_g[64] OC_UNALIGNED;
extern CImage *placemask_images_sgpFreeTail_g;
#endif

// sgbPens_g -- BYTE[32] @0x6D2368, the pen colours gds/palette.cpp loads.
#ifdef OC_DLL_MODE
#ifndef sgbPens_g
#define sgbPens_g (reinterpret_cast<BYTE *>(OC_VA(0x6D2368u)))
#endif
#else
extern BYTE sgbPens_g[32] OC_UNALIGNED;
#endif

// The .lo overlay-attachment files, one pointer per images.dat row per overlay kind.
// LoadGroups loads each row 999 pointers after the last, so a row is `[999 * kind + id]`:
//   row 0  CImage::GetOverlayOffset
//   row 1  init_damage_overlays
//   row 2  ApplySpecialOffset / SetFacing / sub_40F3A0
//   row 3  LoadGroups only

// Hyperion: FIVE rows, not four. LoadGroups loads a fifth, the lift-off dust
// overlays, at 999 * 4 (0x59A270), and group_free_all frees five; the table runs to
// 0x59B20C, four bytes short of sgpShieldOverlayGroups_g. The storage the x64 work
// gives it must hold all five, or the fifth lands in the shield table's
//.
//   row 4  LoadGroups, GetOverlayOffset (bLoType 4)
// ROW 1's LITERAL IS THE BASE MINUS FOUR, and that is MSVC's down-counting loop,
// not a fifth table: init_damage_overlays keeps one induction variable, reads the
// pointer with it BEFORE the `dec` and the byte array with it AFTER, so the
// pointer base carries the compensating -4. Its real base is 0x59739C.
struct lo_file_t {
  /*0x00*/ BYTE  _unmapped_00[8];
  /*0x08*/ DWORD adwFrames[1];   // per-frame byte offset, from the file base
};
static_assert(offsetof(lo_file_t, adwFrames) == 8, "lo_file_t::adwFrames  `[esi+eax*4+8h]` in ApplySpecialOffset, `[edx+8h]` in init_damage_overlays");
static_assert(sizeof(lo_file_t) == 0x0C, "lo_file_t  header + ONE frame slot; the file is variable-length and every body indexes adwFrames past the end, so this pins the STRIDE (4) and the header (8), not the object");

#ifdef OC_DLL_MODE
#ifndef sgpOverlayGroups
#define sgpOverlayGroups (reinterpret_cast<lo_file_t **>(OC_VA(0x596400u)))
#endif
#else
extern lo_file_t *sgpOverlayGroups[5 * 999];
#endif

// Hyperion: an image's overlay offsets of a kind, 0 to 4 the rows of sgpOverlayGroups
// (attack, damage, special, landing dust, liftoff dust) and 5 the shield's, which 1.17 keeps
// in rows of 999 images; a mod's images keep theirs in a table of their own, filled as the
// mod's images load.
const lo_file_t *g_hxModOverlays[6][IMAGE_TYPES_MAX - IMAGE_TYPES_117];
static inline const lo_file_t *ImageOverlay(int nKind, unsigned nImage);  // below sgpShieldOverlayGroups_g
#include "../port/data/port_moddata.h"  // Hyperion: ModDataStartUp, ImageScriptsEnd, ImageScriptsAppend

// Walks the frame's RLE row y and says whether pixel x is opaque.
BOOL frame_clickable_at(PFRAME pFrame, int x, int y)
{
  if ((DWORD)x >= pFrame->bWidth)
    return 0;
  if ((DWORD)y >= pFrame->bHeight)
    return 0;

  const BYTE *p = GrpFrameRow(pFrame, y);  // Hyperion: also a mod's sheet's

  BYTE b;
  do {
    b = *p++;
    if (b & 0x80) {
      x -= b & ~0x80;
    } else if (b & 0x40) {
      x -= b & ~0x40;
      p++;
    } else {
      x -= b;
      p += b;
    }
  } while (x > 0);

  return ~(b >> 7) & 1;
}

int CImage::ClickableAt(int x, int y)
{
  GROUP *pGroup = this->pGroup;
  if (!GroupHasFrame(pGroup, this->wFrameIndex))  // Hyperion: a frame past it (grp.h)
    return 0;
  FRAME *pFrame = &pGroup->frames[this->wFrameIndex];

  x -= this->wMapX;
  if (this->flags.w & 2)
    x = pGroup->wMaxWidth - 2 * pFrame->bOffsetX - x;

  return frame_clickable_at(pFrame, x, y - this->wMapY);
}

// rle_clip_t -- the clip block the blitters take: the source skip and the visible extent,
// four dwords. rle_draw_greenbox's two edge tests are what type them -- nSkipX + nWidth is
// the frame's width, nSkipY + nHeight its height -- and the (WORD) casts at the
// bmp_hline/bmp_vline calls are the truncation those parameters force.
struct rle_clip_t {
  /*0x00*/ int nSkipX;
  /*0x04*/ int nSkipY;
  /*0x08*/ int nWidth;
  /*0x0C*/ int nHeight;
};
static_assert(sizeof(rle_clip_t) == 0x10, "rle_clip_t  four dwords, +0 .. +0xC");
static_assert(offsetof(rle_clip_t, nSkipY) == 0x04, "rle_clip_t::nSkipY  [esi+4h]");
static_assert(offsetof(rle_clip_t, nWidth) == 0x08, "rle_clip_t::nWidth  [esi+8h]");
static_assert(offsetof(rle_clip_t, nHeight) == 0x0C, "rle_clip_t::nHeight [esi+0Ch]");

void __fastcall rle_draw_greenbox(int nX, int nY, FRAME *pFrame,
                                  rle_clip_t *pClip, BYTE bLevel)
{
  _bmp_pen = sgbPens_g[0x12];

  if (pClip->nSkipY == 0)
    bmp_hline(PosU(nX), PosU(nY), (WORD)pClip->nWidth);

  if (pClip->nSkipX == 0)
    bmp_vline(PosU(nX), PosU(nY), (WORD)pClip->nHeight);

  if (pClip->nSkipX + pClip->nWidth == pFrame->bWidth)
    bmp_vline(PosU(nX + pClip->nWidth - 1), PosU(nY), (WORD)pClip->nHeight);

  if (pClip->nHeight + pClip->nSkipY == pFrame->bHeight)
    bmp_hline(PosU(nX), PosU(nY + pClip->nHeight - 1), (WORD)pClip->nWidth);
}

void CImage::ClearPlaceboxes()
{
  for (int i = 0; i < 64; i++) {
    if (sgPlaceboxImages_g[i].pParentSprite != 0) {
      sgPlaceboxImages_g[i].Invalidate();
      CSprite *pSprite = sgPlaceboxImages_g[i].pParentSprite;
      reinterpret_cast<CLink *>(&sgPlaceboxImages_g[i])->Unlink(
          reinterpret_cast<CLink **>(&pSprite->pImageHead),
          reinterpret_cast<CLink **>(&pSprite->pImageTail));
      reinterpret_cast<CLink *>(&sgPlaceboxImages_g[i])->PrependThis(
          reinterpret_cast<CLink **>(&placebox_images_sgpFreeHead_g),
          reinterpret_cast<CLink **>(&placebox_images_sgpFreeTail_g));
      sgPlaceboxImages_g[i].pParentSprite = 0;
    }
    if (sgPlacemaskImages_g[i].pParentSprite != 0) {
      sgPlacemaskImages_g[i].Invalidate();
      CSprite *pSprite = sgPlacemaskImages_g[i].pParentSprite;
      reinterpret_cast<CLink *>(&sgPlacemaskImages_g[i])->Unlink(
          reinterpret_cast<CLink **>(&pSprite->pImageHead),
          reinterpret_cast<CLink **>(&pSprite->pImageTail));
      reinterpret_cast<CLink *>(&sgPlacemaskImages_g[i])->PrependThis(
          reinterpret_cast<CLink **>(&placemask_images_sgpFreeHead_g),
          reinterpret_cast<CLink **>(&placemask_images_sgpFreeTail_g));
      sgPlacemaskImages_g[i].pParentSprite = 0;
    }
  }
}

// Counts, for each overlay group, the offset pairs of its first frame that are not the
// (0x7F, 0x7F) sentinel, and stores the count doubled. The group pointer is read again
// inside the frame loop and the pair goes through an int array, as 1.17 has it.
void __fastcall init_damage_overlays()
{
  int i = NUM_IMAGES;
  do {
    i--;
    lo_file_t *pLo = sgpOverlayGroups[999 + i];
    int nCount = 0;
    sgbNumDmgOverlays_g[i] = (BYTE)nCount;
    if (pLo != 0) {
      int nOff = 0x2C;
      do {
        nOff -= 2;
        const char *p = reinterpret_cast<const char *>(sgpOverlayGroups[999 + i]);
        p += reinterpret_cast<const lo_file_t *>(p)->adwFrames[0] + nOff;
        int pos[2];
        pos[0] = p[0];
        pos[1] = p[1];
        if (pos[0] != 0x7F)
          nCount++;
        else if (pos[1] != 0x7F)
          nCount++;
      } while (nOff != 0);
      if (nCount != 0)
        sgbNumDmgOverlays_g[i] = (BYTE)(nCount + nCount);
    }
  } while (i != 0);
}

void CImage::ApplySpecialOffset()
{
  CImage *pMain = this->pParentSprite->pMainImage;
  const char *p = reinterpret_cast<const char *>(
      ImageOverlay(2, pMain->wImageID));  // Hyperion: sgpOverlayGroups[999 * 2 + ...]
  p += reinterpret_cast<const lo_file_t *>(p)->adwFrames[pMain->wFrameIndex];

  int nPosX = p[0];
  if (pMain->flags.w & 2)
    nPosX = -nPosX;
  int nPosY = p[1];

  if (this->cHorzOffset != (char)nPosX || this->cVertOffset != (char)nPosY) {
    this->flags.b.Dirty = 1;
    this->cHorzOffset = (char)nPosX;
    this->cVertOffset = (char)nPosY;
  }
}

// Set or clear the horizontal mirror, re-seat the draw functions, and re-read the special
// overlay offset when flags bit 7 is set. SetRLE's work is written out on the new flags
// word rather than called.
void CImage::FlipImage(BYTE bFlip)
{
  if (((this->flags.w >> 1) & 1) == bFlip)
    return;
  WORD wFlags = (WORD)(((bFlip & 1) * 2) | (this->flags.w & 0xFFFD));
  this->flags.w = wFlags;
  BYTE bRLE = this->bPaletteType;
  this->bPaletteType = bRLE;
  this->pfnUpdate    = sgUpdateFuncs[bRLE].pfnUpdate;
  if (wFlags & 2)
    this->pfnRender = sgDrawFuncs[bRLE].pfnFlipped;
  else
    this->pfnRender = sgDrawFuncs[bRLE].pfnNormal;
  if (bRLE == 0x11) {
    this->coloringData.rle.bRleFrame = 0x30;
    this->coloringData.rle.bRleWait  = 0x02;
  }
  this->flags.w = (WORD)(wFlags | 1);

  if (this->flags.w & 0x80) {
    CImage *pMain = this->pParentSprite->pMainImage;
    const char *p = reinterpret_cast<const char *>(
        ImageOverlay(2, pMain->wImageID));  // Hyperion: sgpOverlayGroups[999 * 2 + ...]
    p += reinterpret_cast<const lo_file_t *>(p)->adwFrames[pMain->wFrameIndex];
    int nPosX = p[0];
    if (pMain->flags.w & 2)
      nPosX = -nPosX;
    int nPosY = p[1];
    if (this->cHorzOffset != (char)nPosX || this->cVertOffset != (char)nPosY) {
      this->cHorzOffset = (char)nPosX;
      this->cVertOffset = (char)nPosY;
      this->flags.w |= 1;
    }
  }
}

// Clips the vertical half of the update rect to the rows the view draws. Inlined into
// DrawUpdateRect.
static __forceinline void update_rect_clip_y(CImage *pImage, int nTop, int nHeight)
{
  int nGrpY;
  if (nTop < 0) {
    nGrpY = -nTop;
    nHeight += nTop;
    nTop = 0;
  } else {
    nGrpY = 0;
  }
  pImage->wGrpY = (short)nGrpY;
  pImage->wScreenY = (short)nTop;
  // Hyperion: clipped to the rows the map is drawn in, 400 in 1.17.
  if (nHeight >= g_portView.nMapRows - nTop)
    nHeight = g_portView.nMapRows - nTop;
  pImage->wDrawHeight = (WORD)nHeight;
}

// Recomputes the image's map position and its clipped screen rect.
void CImage::DrawUpdateRect()
{
  GROUP *pGroup = this->pGroup;
  FRAME *pFrame = &pGroup->frames[this->wFrameIndex];
  int nHalfWidth = pGroup->wMaxWidth >> 1;
  int nX = PosS(this->pParentSprite->wPosX) + this->cHorzOffset;
  if (this->flags.w & 2)
    nX += nHalfWidth - pFrame->bWidth - pFrame->bOffsetX;
  else
    nX += pFrame->bOffsetX - nHalfWidth;
  this->wMapX = PosS(nX);

  if (!(this->flags.w & 4))
    this->wMapY = PosS(pFrame->_unmapped_01 - (pGroup->wMaxHeight >> 1) +
                          this->cVertOffset + this->pParentSprite->wPosY);

  int nTop = this->wMapY - glScrollPixelY;
  int nWidth = pFrame->bWidth;
  int nHeight = pFrame->bHeight;
  int nGrpX = 0;
  nX = this->wMapX - glScrollPixelX;
  if (nX < 0) {
    nGrpX = -nX;
    nWidth += nX;
    nX = 0;
  }
  this->wGrpX = (short)nGrpX;
  this->wScreenX = PosS(nX);
  if (nWidth >= g_portView.nWidth - nX)  // Hyperion: the view's width, 640 in 1.17
    nWidth = g_portView.nWidth - nX;
  this->wDrawWidth = (WORD)nWidth;

  update_rect_clip_y(this, nTop, nHeight);
}

void CSprite::KillPlacebox()
{
  CImage *pImage = this->OverlayGetImage(0x245, 0x245);
  if (pImage != 0) {
    pImage->Invalidate();
    CSprite *pSprite = pImage->pParentSprite;
    reinterpret_cast<CLink *>(pImage)->Unlink(
        reinterpret_cast<CLink **>(&pSprite->pImageHead),
        reinterpret_cast<CLink **>(&pSprite->pImageTail));
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&placebox_images_sgpFreeHead_g),
        reinterpret_cast<CLink **>(&placebox_images_sgpFreeTail_g));
    pImage->pParentSprite = 0;
  }

  pImage = this->OverlayGetImage(0x247, 0x247);
  if (pImage != 0) {
    pImage->Invalidate();
    CSprite *pSprite = pImage->pParentSprite;
    reinterpret_cast<CLink *>(pImage)->Unlink(
        reinterpret_cast<CLink **>(&pSprite->pImageHead),
        reinterpret_cast<CLink **>(&pSprite->pImageTail));
    reinterpret_cast<CLink *>(pImage)->PrependThis(
        reinterpret_cast<CLink **>(&placemask_images_sgpFreeHead_g),
        reinterpret_cast<CLink **>(&placemask_images_sgpFreeTail_g));
    pImage->pParentSprite = 0;
  }
}

// Seat a placement-mask image on pSprite at dwPos, sized by the placebox, and start its
// script.
void CImage::InitPlacemask(CSprite *pSprite, DWORD dwPos, point16_t size)
{
  this->cHorzOffset = (char)dwPos;
  this->cVertOffset = (char)(dwPos >> 16);

  this->pGroup->frames[0].bOffsetX = 0;
  this->pGroup->frames[0]._unmapped_01 = 0;
  this->pGroup->frames[0].bWidth = (BYTE)size.x;
  this->pGroup->frames[0].bHeight = (BYTE)size.y;
  this->pGroup->wMaxWidth = size.x;
  this->pGroup->wMaxHeight = size.y;

  this->flags.w = 0;
  this->pParentSprite = pSprite;
  // An 8-byte memset, not four field stores, as 1.17 has it.
  memset(&this->wGrpX, 0, 8);

  this->DrawUpdateRect();

  this->flags.b.Dirty = 1;
  this->ForceScript(img_iscriptId[this->wImageID]);

  // CImage::SetAnimation at bAnim = 0, written out as rle_advance_warp_fade does.
  iscript_off_t *pIscript = &this->wIscriptHeader;
  const iscript_off_t anim = ImageScriptAnim(*pIscript, 0);  // Hyperion: and a mod's header

  this->bAnim = 0;
  this->wIscriptOffset = anim;
  this->bWait = 0;
  this->w14 = 0;
  this->RunScript(pIscript, 0, 0);
}

CImage * __fastcall CImage::AllocPlacebox(CSprite *pSprite, WORD wGroupIndex,
                                          DWORD dwPos, int nUnitType)
{
  CImage *pImage = placebox_images_sgpFreeHead_g;
  if (pImage == 0)
    return 0;

  reinterpret_cast<CLink *>(pImage)->Unlink(
      reinterpret_cast<CLink **>(&placebox_images_sgpFreeHead_g),
      reinterpret_cast<CLink **>(&placebox_images_sgpFreeTail_g));
  reinterpret_cast<CLink *>(pSprite->pImageHead)->PrependLink(
      reinterpret_cast<CLink *>(pImage),
      reinterpret_cast<CLink **>(&pSprite->pImageHead));

  pImage->Initialize(0x245, pSprite,
                     *reinterpret_cast<const char *>(&dwPos),
                     *(reinterpret_cast<const char *>(&dwPos) + 2));
  pImage->pGroup = sgpImgGroups[wGroupIndex];
  pImage->InitScript();

  CImage *pMask = placemask_images_sgpFreeHead_g;
  point16_t size;
  size.x = gUnitPlaceboxSize[(WORD)nUnitType].x;
  size.y = gUnitPlaceboxSize[(WORD)nUnitType].y;

  if (pMask != 0) {
    reinterpret_cast<CLink *>(pMask)->Unlink(
        reinterpret_cast<CLink **>(&placemask_images_sgpFreeHead_g),
        reinterpret_cast<CLink **>(&placemask_images_sgpFreeTail_g));
    reinterpret_cast<CLink *>(pSprite->pImageHead)->PrependLink(
        reinterpret_cast<CLink *>(pMask),
        reinterpret_cast<CLink **>(&pSprite->pImageHead));
    pMask->InitPlacemask(pSprite, dwPos, size);
  }

  return pImage;
}

// The images.dat and sprites.dat columns these bodies read, with the pools' GROUP tables
// and the overlay tables, each spelled for either build. sprite_selCircleImg and
// sprite_selCircleOffset are biased by 130 in the source rather than in the symbol: both
// are read at base - 130 with the sprite id, so the subscript is `tbl[wSpriteID - 130]`,
// and the extent is 387 = 517 - 130. sgpOverlayGroups is indexed flat, 999 * kind + id.

// The loaded .lo? overlay file. Only +0x08 is derived: every consumer reads a DWORD table
// of per-frame byte offsets there and then indexes the file from its own base with that
// offset. The eight bytes below it are underived and held only so the +0x08 table lands
// right.

#ifdef OC_DLL_MODE
#ifndef gIMAGESArrayDataTbl_g
#define gIMAGESArrayDataTbl_g (reinterpret_cast<array_data_t*>(OC_VA(0x5011A8u)))
#endif
#ifndef img_gfxTurns
#define img_gfxTurns (reinterpret_cast<BYTE *>(OC_VA(0x5126E8u)))
#endif
#ifndef img_clickable
#define img_clickable (reinterpret_cast<BYTE *>(OC_VA(0x512AD0u)))
#endif
#ifndef gubImageUseScript
#define gubImageUseScript (reinterpret_cast<BYTE *>(OC_VA(0x512EB8u)))
#endif
#ifndef gubImageRLE
#define gubImageRLE (reinterpret_cast<BYTE *>(OC_VA(0x513688u)))
#endif
#ifndef gubImageColorShift
#define gubImageColorShift (reinterpret_cast<BYTE *>(OC_VA(0x513A70u)))
#endif
#ifndef sprite_selCircleImg
#define sprite_selCircleImg (reinterpret_cast<BYTE *>(OC_VA(0x51F450u)))
#endif
#ifndef sprite_selCircleOffset
#define sprite_selCircleOffset (reinterpret_cast<BYTE *>(OC_VA(0x51F5D8u)))
#endif
#ifndef sgHpbarGroups_g
#define sgHpbarGroups_g (reinterpret_cast<GROUP *>(OC_VA(0x592BA8u)))
#endif
#ifndef sgpShieldOverlayGroups_g
#define sgpShieldOverlayGroups_g (reinterpret_cast<const lo_file_t **>(OC_VA(0x59B210u)))
#endif
#else
extern array_data_t gIMAGESArrayDataTbl_g[15] OC_UNALIGNED;
extern BYTE  img_gfxTurns[999];
extern BYTE  img_clickable[999];
extern BYTE  gubImageUseScript[999];
extern BYTE  gubImageRLE[999];
extern BYTE  gubImageColorShift[999];
extern BYTE  sprite_selCircleImg[387] OC_UNALIGNED;
extern BYTE  sprite_selCircleOffset[387] OC_UNALIGNED;
extern GROUP sgHpbarGroups_g[SELECTION_MAX];
extern const lo_file_t *sgpShieldOverlayGroups_g[999] OC_UNALIGNED;
#endif

// Hyperion: the health bars, their groups and the circles of a game of Hyperion's
// selection, 255 units (stage S, h/selectlimits.h); every other game keeps 1.17's pools of 12
// and 80. The circles are a selection's for each human, and 1.17's 80 more.
#include "../port/replays/port_snap.h"  // PORT_SNAP_KEEP
#define SELECTION_CIRCLES_WIDE (SELECTION_HUMANS * SELECTION_WIDE + 80)
CImage g_hxHpbarImages[SELECTION_WIDE];
GROUP g_hxHpbarGroups[SELECTION_WIDE];
CImage g_hxCircleImages[SELECTION_CIRCLES_WIDE];
PORT_SNAP_KEEP(g_hxHpbarImages);  // the trainer's snapshots hold them (port_snap.cpp)
PORT_SNAP_KEEP(g_hxHpbarGroups);
PORT_SNAP_KEEP(g_hxCircleImages);

static inline const lo_file_t *ImageOverlay(int nKind, unsigned nImage) {
  if (nImage >= IMAGE_TYPES_117)
    return g_hxModOverlays[nKind][nImage - IMAGE_TYPES_117];
  return nKind == 5 ? sgpShieldOverlayGroups_g[nImage] : sgpOverlayGroups[nKind * 999 + nImage];
}

// load_array_data / load_game_data / GameLoadImage are declared verbatim from
// src117/h/snd.h and src117/h/glues.h, repeated here rather than included so this TU's
// include closure stays where it is; the spellings must not drift from those headers.
void  load_array_data(const char *pszFile, void *pArrayData);
void *load_game_data(const char *pszFile, void *pBuffer, DWORD *pdwSize,
                     const char *pszTag, int nLine);
void  GameLoadImage(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3, intptr_t nArg4,
                    intptr_t nArg5, intptr_t nArg6);

// init_damage_overlays is one of this TU's own bodies and no src117 header declares it.
void __fastcall init_damage_overlays();

// Free the iscript buffer and every image, overlay and shield .lo table, then
// images.dat's array-data table. Registered as an exit handler.
void group_free_all()
{
  mem_free(sgpScripts, "CImage.cpp", 0x836);
  sgpScripts = NULL;
  group_free_tbl(reinterpret_cast<void **>(sgpImgGroups), 999);
  for (int i = 0; i < 5; i++)
    group_free_tbl(reinterpret_cast<void **>(&sgpOverlayGroups[i * 999]), 999);
  group_free_tbl(reinterpret_cast<void **>(const_cast<lo_file_t **>(sgpShieldOverlayGroups_g)), 999);
  gamedata_free_data(gIMAGESArrayDataTbl_g);
}

// Read overlay nOverlay's (x, y) for this image's frame from its .lo row of
// kind bLoType, mirrored in x when the image is. The byte index is written
// back into nOverlay: held in a local, it becomes the SIB base instead of the
// .lo pointer.
shield_offset_t *CImage::GetOverlayOffset(shield_offset_t *pOff, BYTE bLoType, int nOverlay)
{
  const lo_file_t *pLo = ImageOverlay(bLoType, this->wImageID);  // Hyperion: and a mod's images
  nOverlay = pLo->adwFrames[this->wFrameIndex] + nOverlay * 2;
  const char *pBase = reinterpret_cast<const char *>(pLo);
  shield_offset_t off;
  off.x = pBase[nOverlay];
  if (this->flags.w & 2)
    off.x = -off.x;
  off.y = pBase[nOverlay + 1];
  *pOff = off;
  return pOff;
}

// Read the shield overlay's (x, y) for nDirection from this image's shield .lo
// row. The index goes back into the parameter, as in GetOverlayOffset.
void CImage::GetShieldOffset(shield_offset_t *pOff, int nDirection)
{
  const lo_file_t *pLo = ImageOverlay(5, this->wImageID);  // Hyperion: and a mod's images
  nDirection = pLo->adwFrames[this->bDirection] + nDirection * 2;
  const char *pBase = reinterpret_cast<const char *>(pLo);
  shield_offset_t off;
  off.x = pBase[nDirection];
  off.y = pBase[nDirection + 1];
  *pOff = off;
}

// oracle 0x40FEF0 (79 B). Free and nullary; the last statement tail-jumps into
// init_damage_overlays. The line number handed to load_game_data is 1.17's own
// __LINE__ at that call, 2085, written out.
void LoadGraphics()
{
  // Hyperion: and its size, where a mod's scripts start
  sgpScripts = reinterpret_cast<script_head_t *>(
      load_game_data("scripts\\iscript.bin", NULL, &s_dwScripts117, "CImage.cpp", 2085));
  s_dwScripts = s_dwScripts117;
  load_array_data("arr\\images.dat", gIMAGESArrayDataTbl_g);
  GameLoadImage(reinterpret_cast<intptr_t>("game\\tselect.pcx"), 0,
                reinterpret_cast<intptr_t>(sgSelectPalette_g), 0x18, 0, 0, 0);
  LoadGroups();
  init_damage_overlays();
  ModDataStartUp();  // Hyperion: a mod's images and scripts after 1.17's
}

// Hyperion: a mod's scripts after 1.17's (port_moddata.cpp). A mod's block goes
// after the file's last byte, 1.17's iscript.bin kept as the
// file has it, its id table too, which an EUD map may read; a mod's ids are
// port_moddata.cpp's (ModScriptHeader), and its headers hold DWORDs (ImageScriptAnim).
DWORD ImageScriptsEnd()
{
  return s_dwScripts;
}

bool ImageScriptsAppend(const BYTE *pBlock, DWORD cbBlock)
{
  BYTE *p = static_cast<BYTE *>(SMemAlloc(s_dwScripts + cbBlock, "port_moddata.cpp", 0, 0));
  if (p == NULL)
    return false;
  memcpy(p, sgpScripts, s_dwScripts);
  memcpy(p + s_dwScripts, pBlock, cbBlock);
  mem_free(sgpScripts, "CImage.cpp", 0x836);
  sgpScripts = reinterpret_cast<script_head_t *>(p);
  s_dwScripts += cbBlock;
  return true;
}

iscript_off_t ImageScriptAnim(iscript_off_t header, int nAnim)
{
  const BYTE *p = reinterpret_cast<const BYTE *>(sgpScripts) + header + 8;
  // Hyperion: a slot inside the scripts; a mod's orders.dat may name any animation
  const DWORD dwWidth = header < s_dwScripts117 ? 2 : 4;
  const DWORD dwSlot = header + 8 + static_cast<DWORD>(nAnim) * dwWidth;
  if (nAnim < 0 || nAnim > 0xFF || dwSlot + dwWidth > s_dwScripts)
    return 0;
  if (header < s_dwScripts117)
    return *reinterpret_cast<const WORD *>(p + nAnim * 2);
  return static_cast<iscript_off_t>(ScriptDword(p + nAnim * 4));
}

// oracle 0x40EF80 (108 B). The middle is CImage::SetRLE inlined, including its
// `bPaletteType == 0x11` arm, which is dead here because the guard above has already
// restricted the value to 2..7.
void CImage::InheritDrawInfo(CImage *pSrc)
{
  if (pSrc->bPaletteType < 2)
    return;
  if (pSrc->bPaletteType > 7)
    return;

  this->SetRLE(pSrc->bPaletteType);
  this->coloringData.rle.bRleFrame = pSrc->coloringData.rle.bRleFrame;
  this->coloringData.rle.bRleWait  = pSrc->coloringData.rle.bRleWait;
}

// Seat this image as one of the hp-bar pool's images: fixed image id, one GROUP slot per
// bar type, palette type 0x0B, and a restarted script. SetRLE is inlined and
// constant-folded.
void CImage::InitHPBar(int nBarType)
{
  this->wImageID = 0x230;

  // Hyperion: or the group of the same bar in the pool of Hyperion's selection (stage S)
  const bool bWide = this >= g_hxHpbarImages && this < g_hxHpbarImages + SELECTION_WIDE;
  GROUP *pGroup = bWide ? &g_hxHpbarGroups[nBarType] : &sgHpbarGroups_g[nBarType];
  this->pGroup  = pGroup;
  *reinterpret_cast<WORD *>(pGroup) = 1;

  this->flags.w &= 0xFFC7;
  this->wFrameSet   = 0;
  this->wFrameIndex = 0;
  this->cHorzOffset = 0;
  this->bDirection  = 0;
  this->SetRLE(0x0B);
  this->ForceScript(img_iscriptId[this->wImageID]);
}

// oracle 0x40FB50 (141 B). It tail-jumps into CImage::DrawUpdateRect, so that is the
// last statement. CImage::SetRLE is inlined with a variable argument,
// gubImageRLE[wImageID], and the whole bit expression is computed in WORD width.
void CImage::InitScript()
{
  WORD wImageID = this->wImageID;

  this->SetRLE(gubImageRLE[wImageID]);
  this->flags.w = (WORD)(((WORD)(gubImageUseScript[wImageID] & 1) << 4)
                         | (this->flags.w & 0xFFEF));
  this->ForceScript(img_iscriptId[wImageID]);
  this->SetAnimation(0);
  this->DrawUpdateRect();
}

// oracle 0x411730 (145 B). The parameter is a BYTE. bIsInit is a real local: 1.17
// computes the predicate once and tests it twice. The tail -- store bAnim, read the
// animation's script offset out of the iscript.bin header table, clear the wait and the
// return offset, run the script from the top -- is written out again in WarpFlashInit
// and SelectInit, while InitScript calls this body; noinline keeps that call.
__declspec(noinline) void CImage::SetAnimation(BYTE bAnim)
{
  BOOL bIsInit = (bAnim == 1);

  if (bIsInit) {
    if (this->bAnim == 1)
      return;
  }

  if (!(this->flags.w & 0x10)) {
    if (bAnim != 0) {
      if (!bIsInit)
        return;
    }
  }

  BYTE bCur = this->bAnim;

  if (bAnim == bCur) {
    if (bAnim == 0x0B)
      return;
    if (bAnim == 0x13)
      return;
  }

  if (bAnim == 5) {
    if (bCur != bAnim && bCur != 2)
      bAnim = 2;
  } else if (bAnim == 6) {
    if (bCur != bAnim && bCur != 3)
      bAnim = 3;
  }

  iscript_off_t *pIscript = &this->wIscriptHeader;

  // Hyperion: a mod's image whose art the player lacks has its base's script
  // (port_moddata.cpp, ImageArt), which may have no entry for an animation the mod's unit
  // plays, a Hybrid Reaver's cast on the Ultralisk's script; 1.17 would run the scripts from
  // their first byte. The image keeps the animation it has.
  if (this->wImageID >= IMAGE_TYPES_117 && ImageScriptAnim(*pIscript, bAnim) == 0)
    return;

  this->bAnim = bAnim;
  this->wIscriptOffset = ImageScriptAnim(*pIscript, bAnim);  // Hyperion: and a mod's header
  this->bWait = 0;
  this->w14 = 0;
  this->RunScript(pIscript, 0, 0);
}

// oracle 0x411840 (134 B).
//
// The parameter is dead in 1.17: nothing reads it, and the iscript id handed to the first
// ForceScript is the literal 0xC1 while the only caller passes 0xD2. Reproduced as it is.
// The SetAnimation tail at bAnim 0 and SetRLE at 0x0C are written out, not called.
void CImage::WarpFlashInit(int nImageID)
{
  this->ForceScript(0xC1);

  iscript_off_t *pIscript = &this->wIscriptHeader;
  const iscript_off_t anim = ImageScriptAnim(this->wIscriptHeader, 0);  // Hyperion

  this->bAnim = 0;
  this->wIscriptOffset = anim;
  this->bWait = 0;
  this->w14 = 0;
  this->RunScript(pIscript, 0, 0);

  this->ForceScript(img_iscriptId[this->wImageID]);
  this->SetRLE(0x0C);
  this->RunScript(pIscript, 0, 0);
}

// oracle 0x411460 (202 B). Both sprites.dat columns are biased by 130 in the source,
// not in the symbol. The three zeroing statements are CImage::Initialize's three,
// written out, and the +0x30 dword is zeroed and then immediately overwritten with
// bSelIndex -- a redundant store 1.17 really makes.
void CImage::SelectInit(CSprite *pSprite, BYTE bSelIndex, int nImageID)
{
  iscript_off_t *pIscript = &this->wIscriptHeader;

  this->wImageID =
      (WORD)(sprite_selCircleImg[pSprite->wSpriteID - 130] + nImageID);
  this->pGroup        = sgpImgGroups[this->wImageID];
  this->pParentSprite = pSprite;
  this->cVertOffset   = (char)sprite_selCircleOffset[pSprite->wSpriteID - 130];

  memset(&this->wGrpX, 0, 8);
  memset(&this->coloringData, 0, sizeof(this->coloringData));  // Hyperion: 4 in 1.17
  memset(pIscript, 0, ISCRIPT_STATE_BYTES);  // Hyperion: 8 in 1.17

  this->coloringData.dw = bSelIndex;
  this->SetRLE(0x0D);
  this->DrawUpdateRect();

  this->ForceScript(img_iscriptId[this->wImageID]);

  const iscript_off_t anim = ImageScriptAnim(*pIscript, 0);  // Hyperion

  this->bAnim = 0;
  this->wIscriptOffset = anim;
  this->bWait = 0;
  this->w14 = 0;
  this->RunScript(pIscript, 0, 0);
}

// Turn the image to one of 32 directions, the upper half mirrored, re-seat the draw
// functions and the frame, and re-read the special overlay offset. The draw functions come
// from SetRLE on the image's own palette type, inlined.
void CImage::FaceDirection(BYTE bDirection)
{
  if (!(reinterpret_cast<const BYTE *>(&this->flags)[0] & 8))
    return;

  BYTE bFlip;
  BYTE bDir = ImageFold(this, bDirection, &bFlip);  // Hyperion: a 32's unmirrored

  WORD wFlags = this->flags.w;
  if (((wFlags >> 1) & 1) == bFlip && this->bDirection == bDir)
    return;

  this->bDirection = bDir;
  this->flags.w = (WORD)(((bFlip & 1) * 2) | (wFlags & 0xFFFD));
  this->SetRLE(this->bPaletteType);

  int nIdx = ImageFrame(this, this->wFrameSet, bDir);  // Hyperion: a 32's

  if (this->wFrameIndex != nIdx) {
    this->wFrameIndex = (WORD)nIdx;
    this->flags.w |= 1;
  }

  if (this->flags.w & 0x80)
    this->ApplySpecialOffset();
}

// oracle 0x40FC50 (178 B). The flags word is stored three times, the first two of them
// 1.17's own dead stores, and every intermediate is WORD-width. The two images.dat
// tests at the tail read their index from different places, the first from the field
// just stored and the second from the parameter. The colour-shift index is biased by
// one: the table it picks is light_g[k - 1].
void CImage::Initialize(int nImageID, CSprite *pSprite, char x, char y)
{
  // Hyperion: a mod's script may name any image; one the game lacks is drawn as image 0
  // rather than read past the tables
  if (nImageID < 0 || !ImageTypeValid(static_cast<unsigned>(nImageID)))
    nImageID = 0;
  this->wImageID = (WORD)nImageID;
  this->pGroup   = sgpImgGroups[nImageID];

  this->flags.w = 0;
  this->flags.b.GfxTurns  = img_gfxTurns[nImageID] & 1;
  this->flags.b.Clickable = img_clickable[nImageID] & 1;

  this->wFrameSet   = 0;
  this->wFrameIndex = 0;
  this->bDirection  = 0;
  this->cHorzOffset = x;
  this->cVertOffset = y;
  this->pParentSprite = pSprite;

  memset(&this->wGrpX, 0, 8);
  memset(&this->coloringData, 0, sizeof(this->coloringData));  // Hyperion: 4 in 1.17
  memset(&this->wIscriptHeader, 0, ISCRIPT_STATE_BYTES);  // Hyperion: 8 in 1.17

  if (gubImageRLE[this->wImageID] == 0x0E)
    this->coloringData.dw = pSprite->bCreator;

  // Hyperion: a remapping among light_g's 7
  if (gubImageRLE[nImageID] == 9 && gubImageColorShift[nImageID] >= 1 && gubImageColorShift[nImageID] <= 7)
    this->coloringData.dw = reinterpret_cast<DWORD_PTR>(
        light_g[gubImageColorShift[nImageID] - 1].pData);
}

// Spawn a thingy of wType at this image's offset plus (relX, relY) from its sprite, at
// elevation bElevation. The offset is added into the parameter first and the sprite
// position second, one statement each, as 1.17 has it.
CThingy *CImage::CreateThingy(WORD wType, int relX, int relY, int bElevation)
{
  relX += this->cHorzOffset;
  CSprite *pSprite = this->pParentSprite;
  int px = PosS(pSprite->wPosX);
  int py = pSprite->wPosY;
  relX += px;
  relY += this->cVertOffset;
  relY += py;
  CThingy *pThingy = CThingy::AllocThingy(wType, relX, relY, 0);

  if (pThingy == NULL)
    return pThingy;

  pThingy->pSprite->bElevationLevel = bElevation;
  pThingy->UpdateVisibility();

  return pThingy;
}

// The iscript cursor RunScript advances: CImage+0x10..+0x17, or a free-standing
// copy (CUnitPathUtil.cpp's sgPathScript_g). +0x04 is the `call` return offset.
// Hyperion: the struct is CImage.h's, with offsets of 32 bits on 64.
OC_X86_LAYOUT(offsetof(iscript_state_t, wIscriptHeader) == 0x00, "iscript_state_t::wIscriptHeader");
OC_X86_LAYOUT(offsetof(iscript_state_t, wIscriptOffset) == 0x02, "iscript_state_t::wIscriptOffset `movzx esi, word ptr [edx+2h]`");
OC_X86_LAYOUT(offsetof(iscript_state_t, wUnk04) == 0x04, "iscript_state_t::wUnk04 `mov word ptr [edx+4h], si`");
OC_X86_LAYOUT(offsetof(iscript_state_t, bAnim) == 0x06, "iscript_state_t::bAnim");
OC_X86_LAYOUT(offsetof(iscript_state_t, bWait) == 0x07, "iscript_state_t::bWait `mov al, byte ptr [edx+7h]`");
// and an image's cursor is the struct on every build
static_assert(offsetof(CImage, wIscriptOffset) - offsetof(CImage, wIscriptHeader) == offsetof(iscript_state_t, wIscriptOffset) &&
              offsetof(CImage, w14) - offsetof(CImage, wIscriptHeader) == offsetof(iscript_state_t, wUnk04) &&
              offsetof(CImage, bAnim) - offsetof(CImage, wIscriptHeader) == offsetof(iscript_state_t, bAnim) &&
              offsetof(CImage, bWait) - offsetof(CImage, wIscriptHeader) == offsetof(iscript_state_t, bWait),
              "CImage's iscript cursor");

// gpImageBullet @0x543308 (the bullet whose script runs), guwSpriteImage
// @0x51EAA8 (sprites.dat image ids) and gbIsExpansion @0x536720: unregistered,
// sheet names. g_MapTileset is declared again further down this file.
#ifdef OC_DLL_MODE
#ifndef gpImageBullet
#define gpImageBullet (*reinterpret_cast<CBullet **>(OC_VA(0x543308u)))
#endif
#ifndef guwSpriteImage
#define guwSpriteImage (reinterpret_cast<WORD *>(OC_VA(0x51EAA8u)))
#endif
#ifndef g_MapTileset
#define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
#endif
#else
extern CBullet *gpImageBullet;
extern WORD guwSpriteImage[517];
extern WORD g_MapTileset;
#endif
#ifdef OC_DLL_MODE
#ifndef gbIsExpansion
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#endif
#else
extern BYTE gbIsExpansion;
#endif

// A BYTE temporary: the attacker's player is passed one byte wide.
static inline BYTE unit_player(CUnit *p) { return p->bPlayer; }

// Runs the image's iscript until a wait, an end or a return; with a2 set it only walks the
// operands (randcondjmp still rolls). Operands are read after the cursor advance, as
// [esi-N], so each case steps pScript first and reads back.
void CImage::RunScript(void *pIscript, int a2, intptr_t a3)
{
  iscript_state_t *pState = static_cast<iscript_state_t *>(pIscript);
  const BYTE *pScript;

  if (pState->bWait != 0) {
    pState->bWait--;
    return;
  }
  pScript = reinterpret_cast<const BYTE *>(sgpScripts) + pState->wIscriptOffset;
  for (;;) {
    switch (*pScript++) {
    case 0x00: {  // playfram
      pScript += 2;
      WORD wFrame = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2)
        SetFrame(wFrame);
      break;
    }
    case 0x40: {  // warpoverlay
      pScript += 2;
      if (!a2) {
        WORD wFrame = reinterpret_cast<const WORD *>(pScript)[-1];
        flags.b.Dirty = 1;
        coloringData.dw = wFrame;
      }
      break;
    }
    case 0x01: {  // playframtile
      pScript += 2;
      WORD wFrame = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2) {
        wFrame += g_MapTileset;
        if (wFrame < (WORD)(*reinterpret_cast<const WORD *>(pGroup) & 0x7FFF))
          SetFrame(wFrame);
      }
      break;
    }
    case 0x02: {  // sethorpos
      char x = *pScript++;
      if (!a2 && cHorzOffset != x) {
        flags.b.Dirty = 1;
        cHorzOffset = x;
      }
      break;
    }
    case 0x03: {  // setvertpos
      char y = *pScript++;
      if (!a2) {
        CUnit *pUnit = gpImageUnit;
        if (!(pUnit && (pUnit->dwFlags & 0x300)) && cVertOffset != y) {
          flags.b.Dirty = 1;
          cVertOffset = y;
        }
      }
      break;
    }
    case 0x04: {  // setpos
      char x = pScript[0];
      pScript += 2;
      char y = pScript[-1];
      if (!a2)
        SetOffset(x, y);
      break;
    }
    case 0x06: {  // waitrand
      BYTE bMin = pScript[0];
      pScript += 2;
      BYTE bMax = pScript[-1];
      if (a2)
        break;
      BYTE bWait = (BYTE)net_rand(3) % (bMax - bMin + 1) + bMin - 1;
      pState->wIscriptOffset =
          (iscript_off_t)(pScript - reinterpret_cast<const BYTE *>(sgpScripts));
      pState->bWait = bWait;
      return;
    }
    case 0x07:  // goto
      pScript = reinterpret_cast<const BYTE *>(sgpScripts) +
                *reinterpret_cast<const WORD *>(pScript);
      break;
    case 0x08: {  // imgol
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2)
        CreateOverlay(wImage, pParentSprite, cHorzOffset + x, cVertOffset + y, 1);
      break;
    }
    case 0x09: {  // imgul
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2)
        CreateOverlay(wImage, pParentSprite, cHorzOffset + x, cVertOffset + y, 0);
      break;
    }
    case 0x3D: {  // imgulnextid
      char x = pScript[0];
      pScript += 2;
      char y = pScript[-1];
      if (!a2)
        CreateOverlay(wImageID + 1, pParentSprite, cHorzOffset + x,
                      cVertOffset + y, 0);
      break;
    }
    case 0x0A: {  // imgolorig
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      pScript += 2;
      if (!a2) {
        CImage *pOverlay = CreateOverlay(wImage, pParentSprite, 0, 0, 1);
        if (pOverlay)
          pOverlay->sub_40F3A0(1);
      }
      break;
    }
    case 0x0B: {  // switchul
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      pScript += 2;
      if (!a2) {
        CImage *pOverlay = CreateOverlay(wImage, pParentSprite, 0, 0, 0);
        if (pOverlay)
          pOverlay->sub_40F3A0(1);
      }
      break;
    }
    case 0x0D: {  // imgoluselo
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      BYTE bLo = pScript[2];
      pScript += 4;
      char cOverlay = pScript[-1];
      if (!a2) {
        shield_offset_t off;
        GetOverlayOffset(&off, bLo, cOverlay);
        CreateOverlay(wImage, pParentSprite, cHorzOffset + off.x,
                      cVertOffset + off.y, 1);
      }
      break;
    }
    case 0x0E: {  // imguluselo
      WORD wImage = *reinterpret_cast<const WORD *>(pScript);
      BYTE bLo = pScript[2];
      pScript += 4;
      char cOverlay = pScript[-1];
      if (!a2) {
        shield_offset_t off;
        GetOverlayOffset(&off, bLo, cOverlay);
        CreateOverlay(wImage, pParentSprite, cHorzOffset + off.x,
                      cVertOffset + off.y, 0);
      }
      break;
    }
    case 0x42: {  // grdsprol
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2) {
        upos_t wy = pParentSprite->wPosY;
        upos_t wx = pParentSprite->wPosX;
        if (mtx_passable(0, PosU(cHorzOffset + x + wx),
                       PosU(cVertOffset + y + wy)))
          CreateThingy(wSprite, x, y, pParentSprite->bElevationLevel + 1);
      }
      break;
    }
    case 0x0F: {  // sprol
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2) {
        if (gpImageBullet && gpImageBullet->pAttacker
            && gpImageBullet->pAttacker->IsGoliathType()
            && (get_upgrade_level(unit_player(gpImageBullet->pAttacker), 0x36)
                || ((gulUnitTypeFlags[gpImageBullet->pAttacker->wType] & 0x40)
                    && gbIsExpansion)))
          CreateThingy(0x1F9, x, y, pParentSprite->bElevationLevel + 1);
        else
          CreateThingy(wSprite, x, y, pParentSprite->bElevationLevel + 1);
      }
      break;
    }
    case 0x12: {  // uflunstable
      pScript += 2;
      WORD wFlingy = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2) {
        upos_t wy = pParentSprite->wPosY;
        upos_t wx = pParentSprite->wPosX;
        CFlingy *pFlingy = CFlingy::AllocFlingy(
            wFlingy, cHorzOffset + PosS(wx), cVertOffset + PosS(wy), 0);
        if (pFlingy) {
          pFlingy->FaceAnywhere();
          reinterpret_cast<CThingy *>(pFlingy)->UpdateVisibility();
        }
      }
      break;
    }
    case 0x15: {  // sproluselo
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      pScript += 3;
      BYTE bOverlay = pScript[-1];
      if (!a2) {
        shield_offset_t off;
        CThingy *pThingy;
        GetOverlayOffset(&off, bOverlay, 0);
        BYTE bElev = pParentSprite->bElevationLevel;
        pThingy = CreateThingy(wSprite, off.x, off.y, bElev + 1);
        if (pThingy)
          pThingy->pSprite->FaceDirection((flags.w & 2) ? 0x20 - bDirection
                                                        : bDirection);
      }
      break;
    }
    case 0x10: {  // highsprol
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2)
        CreateThingy(wSprite, x, y, pParentSprite->bElevationLevel - 1);
      break;
    }
    case 0x11: {  // lowsprul
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2)
        CreateThingy(wSprite, x, y, 1);
      break;
    }
    case 0x13: {  // spruluselo
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2) {
        CThingy *pThingy;
        CUnit *pUnit = gpImageUnit;
        if (pUnit && (pUnit->dwFlags & 0x300)
            && !img_drawIfCloaked[guwSpriteImage[wSprite]])
          break;
        BYTE bElev = pParentSprite->bElevationLevel;
        pThingy = CreateThingy(wSprite, x, y, bElev);
        if (pThingy)
          pThingy->pSprite->FaceDirection((flags.w & 2) ? 0x20 - bDirection
                                                        : bDirection);
      }
      break;
    }
    case 0x14: {  // sprul
      WORD wSprite = *reinterpret_cast<const WORD *>(pScript);
      char x = pScript[2];
      pScript += 4;
      char y = pScript[-1];
      if (!a2) {
        CThingy *pThingy;
        CUnit *pUnit = gpImageUnit;
        if (pUnit && (pUnit->dwFlags & 0x300)
            && !img_drawIfCloaked[guwSpriteImage[wSprite]])
          break;
        pThingy = CreateThingy(wSprite, x, y,
                               (BYTE)(pParentSprite->bElevationLevel - 1));
        if (pThingy)
          pThingy->pSprite->FaceDirection((flags.w & 2) ? 0x20 - bDirection
                                                        : bDirection);
      }
      break;
    }
    case 0x38: {  // creategasoverlays
      char cOverlay = *pScript++;
      if (!a2) {
        shield_offset_t off;
        WORD wImage;
        CImage *pNew;
        GetOverlayOffset(&off, 2, cOverlay);
        wImage = (gpImageUnit->wResourceAmount == 0 ? 0x1B3 : 0x1AE) + cOverlay;
        pNew = AllocImage();
        if (pNew) {
          reinterpret_cast<CLink *>(this)->PrependLink(
              reinterpret_cast<CLink *>(pNew),
              reinterpret_cast<CLink **>(&pParentSprite->pImageHead));
          pNew->Initialize(wImage, pParentSprite, cHorzOffset + off.x,
                           cVertOffset + off.y);
          pNew->InitScript();
        }
      }
      break;
    }
    case 0x39: {  // pwrupcondjmp
      pScript += 2;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if (pParentSprite && pParentSprite->pMainImage != this)
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      break;
    }
    case 0x16:  // end
      if (!a2) {
        Kill();
        pState->wIscriptOffset =
            (iscript_off_t)(pScript - reinterpret_cast<const BYTE *>(sgpScripts));
        return;
      }
      break;
    case 0x17: {  // setflipstate
      pScript++;
      BYTE bFlip = pScript[-1];
      if (!a2)
        FlipImage(bFlip);
      break;
    }
    case 0x18: {  // playsnd
      pScript += 2;
      WORD wSound = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2)
        PlaySnd(wSound);
      break;
    }
    case 0x1C:  // attackmelee, then its sound list as playsndrand
      if (!a2)
        gpImageUnit->HitMelee();
      // fall through
    case 0x19: {  // playsndrand
      BYTE bCount = *pScript++;
      if (a2) {
        pScript += bCount * 2;
      } else {
        BYTE bPick = (DWORD)net_rand(4) % bCount;
        WORD wSound;
        pScript += bPick * 2 + 2;
        wSound = reinterpret_cast<const WORD *>(pScript)[-1];
        pScript += (BYTE)(bCount - bPick - 1) * 2;
        PlaySnd(wSound);
      }
      break;
    }
    case 0x1A: {  // playsndbtwn
      WORD wMin = *reinterpret_cast<const WORD *>(pScript);
      pScript += 4;
      WORD wMax = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2)
        PlaySnd((DWORD)net_rand(5) % (wMax - wMin + 1) + wMin);
      break;
    }
    case 0x1D:  // followmaingraphic
      if (!a2) {
        CImage *pMain = pParentSprite->pMainImage;
        if (pMain && (wFrameIndex != pMain->wFrameIndex
                      || pMain->flags.b.Mirror != flags.b.Mirror)) {
          wFrameSet = pMain->wFrameSet;
          ImageTakeDirection(this, pMain);  // Hyperion: between a 17 and a 32
          SetRLE(bPaletteType);
          CalcDisplayNo();
        }
      }
      break;
    case 0x2B: {  // engframe
      pScript++;
      BYTE bFrame = pScript[-1];
      if (!a2) {
        CImage *pMain;
        wFrameSet = bFrame;
        pMain = pParentSprite->pMainImage;
        ImageTakeDirection(this, pMain);  // Hyperion: between a 17 and a 32
        SetRLE(bPaletteType);
        CalcDisplayNo();
      }
      break;
    }
    case 0x2C: {  // engset
      BYTE bSet = *pScript++;
      if (!a2) {
        CImage *pMain = pParentSprite->pMainImage;
        wFrameSet = pMain->wFrameSet + ImageSets(pMain) * bSet;  // Hyperion: a 32's rows
        ImageTakeDirection(this, pMain);  // Hyperion: between a 17 and a 32
        SetRLE(bPaletteType);
        CalcDisplayNo();
      }
      break;
    }
    case 0x2D:  // __2d
      if (!a2)
        sub_417EF0();
      break;
    case 0x33:  // tmprmgraphicend
      if (!a2)
        Show();
      break;
    case 0x32:  // tmprmgraphicstart
      if (!a2)
        Hide();
      break;
    case 0x24: {  // sigorder
      pScript++;
      BYTE bSignal = pScript[-1];
      if (!a2)
        gpImageTargeter->bOrderFlags |= bSignal;
      break;
    }
    case 0x41: {  // orderdone
      pScript++;
      BYTE bSignal = pScript[-1];
      if (!a2)
        gpImageTargeter->bOrderFlags &= ~bSignal;
      break;
    }
    case 0x1B:  // domissiledmg
    case 0x44:  // dogrddamage
      if (!a2)
        gpImageBullet->Damage();
      break;
    case 0x1F: {  // turnccwise
      pScript++;
      BYTE bSteps = pScript[-1];
      if (!a2)
        reinterpret_cast<CFlingy *>(gpImageUnit)->CCTurn(bSteps);
      break;
    }
    case 0x20: {  // turncwise
      pScript++;
      BYTE bSteps = pScript[-1];
      if (!a2)
        reinterpret_cast<CFlingy *>(gpImageUnit)->CTurn(bSteps);
      break;
    }
    case 0x21:  // turn1cwise
      if (!a2 && gpImageUnit->pOrderTarget == NULL)
        reinterpret_cast<CFlingy *>(gpImageUnit)->CTurn(1);
      break;
    case 0x22: {  // turnrand
      BYTE bSteps = *pScript++;
      if (!a2) {
        if ((net_rand(6) & 3) == 1)
          reinterpret_cast<CFlingy *>(gpImageUnit)->CCTurn(bSteps);
        else
          reinterpret_cast<CFlingy *>(gpImageUnit)->CTurn(bSteps);
      }
      break;
    }
    case 0x23: {  // setspawnframe
      pScript++;
      if (!a2)
        reinterpret_cast<CFlingy *>(gpImageUnit)->SetSpawnFacing(
            *reinterpret_cast<const CFlingy::spawn_dir_t *>(pScript - 1));
      break;
    }
    case 0x1E: {  // randcondjmp
      BYTE bChance = pScript[0];
      pScript += 3;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if ((net_rand(7) & 0xFFu) <= bChance)
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      break;
    }
    case 0x3A: {  // trgtrangecondjmp
      WORD wRange = *reinterpret_cast<const WORD *>(pScript);
      pScript += 4;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2 && gpImageUnit->pOrderTarget != NULL) {
        int x;
        int y;
        gpImageUnit->sub_420970(&x, &y);
        if (reinterpret_cast<CTarget *>(gpImageUnit)->XYInRadius(wRange, x, y))
          pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      }
      break;
    }
    case 0x3B: {  // trgtarccondjmp
      WORD wAngle = reinterpret_cast<const WORD *>(pScript)[0];
      WORD wSpread = reinterpret_cast<const WORD *>(pScript)[1];
      pScript += 6;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2) {
        CUnit *pUnit = gpImageUnit;
        CUnit *pTarget = pUnit->pOrderTarget;
        if (pTarget) {
          // Keeps pUnit in its stack slot, so pIscript stays in EDX on the null edge.
          _ReadWriteBarrier();
          upos_t ty = pTarget->pSprite->wPosY;
          upos_t tx = pTarget->pSprite->wPosX;
          upos_t uy = pUnit->pSprite->wPosY;
          upos_t ux = pUnit->pSprite->wPosX;
          int nAngle = AngleBetween(PosS(ux), PosS(uy), PosS(tx), PosS(ty));
          if ((DWORD)get_angle(nAngle, wAngle) < wSpread)
            pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
        }
      }
      break;
    }
    case 0x3C: {  // curdirectcondjmp
      WORD wAngle = reinterpret_cast<const WORD *>(pScript)[0];
      WORD wSpread = reinterpret_cast<const WORD *>(pScript)[1];
      pScript += 6;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2) {
        BYTE bDir = gpImageUnit->bUnion21;
        if ((DWORD)get_angle(bDir, wAngle) < wSpread)
          pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      }
      break;
    }
    case 0x25: {  // attackwith
      pScript++;
      BYTE bArm = pScript[-1];
      if (!a2)
        gpImageUnit->AttackWith(bArm);
      break;
    }
    case 0x26:  // attack
      if (!a2) {
        CUnit *pTarget = gpImageUnit->pOrderTarget;
        if (pTarget && (pTarget->dwFlags & 4))
          gpImageUnit->AttackWith(2);
        else
          gpImageUnit->AttackWith(1);
      }
      break;
    case 0x27:  // castspell
      if (!a2 && WeaponTypeValid(gubOrderWeapon[gpImageUnit->bOrderId])  // Hyperion: < 0x82, and a mod's
          && !gpImageUnit->CanFireSpell())
        gpImageUnit->FireSpell(gubOrderWeapon[gpImageUnit->bOrderId]);
      break;
    case 0x28: {  // useweapon
      pScript++;
      BYTE bWeapon = pScript[-1];
      if (!a2)
        gpImageUnit->FireWeapon(bWeapon);
      break;
    }
    case 0x31: {  // attkshiftproj
      BYTE bShift = *pScript++;
      if (!a2) {
        wpn_forwardOffset_g[gpImageUnit->GetWeaponGndEx()] = bShift;
        gpImageUnit->AttackWith(1);
      }
      break;
    }
    case 0x2A:  // gotorepeatattk
      if (!a2) {
        CUnit *pUnit = gpImageUnit;
        if (pUnit)
          pUnit->bMoveFlags &= ~8;
      }
      break;
    case 0x29: {  // move
      int nSpeed = *pScript++ << 8;
      if (a3)
        *reinterpret_cast<int *>(a3) =
            gpImageUnit->ModifyGivenMovementSpeed(nSpeed);
      if (!a2)
        reinterpret_cast<CFlingy *>(gpImageUnit)
            ->ForceSpeed(gpImageUnit->ModifyGivenMovementSpeed(nSpeed));
      break;
    }
    case 0x30:  // ignorerest
      if (!a2) {
        if (gpImageUnit->pOrderTarget != NULL) {
          pState->wIscriptOffset = (iscript_off_t)(pScript - 1 -
              reinterpret_cast<const BYTE *>(sgpScripts));
          pState->bWait = 10;
          return;
        }
        gpImageUnit->BreakAnim();
      }
      break;
    case 0x2E:  // nobrkcodestart
      if (!a2) {
        CUnit *pUnit = gpImageUnit;
        pUnit->dwFlags |= 0x2000;
        pUnit->pSprite->flags.b |= 0x80;
      }
      break;
    case 0x2F:  // nobrkcodeend
      if (!a2 && gpImageUnit)
        gpImageUnit->EndNoBreakCodeSection();
      break;
    case 0x3F: {  // liftoffcondjmp
      pScript += 2;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2 && (gpImageUnit->dwFlags & 4))
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      break;
    }
    case 0x34: {  // setfldirect
      pScript++;
      BYTE bDir = pScript[-1];
      if (!a2)
        reinterpret_cast<CFlingy *>(gpImageUnit)->FaceDirection(bDir * 8);
      break;
    }
    case 0x35: {  // call
      pScript += 2;
      WORD wTarget = reinterpret_cast<const WORD *>(pScript)[-1];
      pState->wUnk04 =
          (iscript_off_t)(pScript - reinterpret_cast<const BYTE *>(sgpScripts));
      pScript = reinterpret_cast<const BYTE *>(sgpScripts) + wTarget;
      break;
    }
    case 0x36:  // return
      pScript = reinterpret_cast<const BYTE *>(sgpScripts) + pState->wUnk04;
      break;
    // Hyperion: the forms of 1.17's jumps and of useweapon a mod's scripts are
    // compiled to (port_iscript.cpp): a jump's target a DWORD, past 64 KiB, and the weapon a
    // WORD, past 255. Each acts as 1.17's opcode of its name.
    case 0x45:  // goto
      pScript = reinterpret_cast<const BYTE *>(sgpScripts) + ScriptDword(pScript);
      break;
    case 0x46: {  // randcondjmp
      const BYTE bChance = pScript[0];
      const DWORD dwTarget = ScriptDword(pScript + 1);
      pScript += 5;
      if ((net_rand(7) & 0xFFu) <= bChance)
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      break;
    }
    case 0x47: {  // pwrupcondjmp
      const DWORD dwTarget = ScriptDword(pScript);
      pScript += 4;
      if (pParentSprite && pParentSprite->pMainImage != this)
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      break;
    }
    case 0x48: {  // trgtrangecondjmp
      const WORD wRange = *reinterpret_cast<const WORD *>(pScript);
      const DWORD dwTarget = ScriptDword(pScript + 2);
      pScript += 6;
      if (!a2 && gpImageUnit->pOrderTarget != NULL) {
        int x;
        int y;
        gpImageUnit->sub_420970(&x, &y);
        if (reinterpret_cast<CTarget *>(gpImageUnit)->XYInRadius(wRange, x, y))
          pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      }
      break;
    }
    case 0x49: {  // trgtarccondjmp
      const WORD wAngle = reinterpret_cast<const WORD *>(pScript)[0];
      const WORD wSpread = reinterpret_cast<const WORD *>(pScript)[1];
      const DWORD dwTarget = ScriptDword(pScript + 4);
      pScript += 8;
      if (!a2 && gpImageUnit->pOrderTarget != NULL) {
        const CSprite *pUs = gpImageUnit->pSprite;
        const CSprite *pThem = gpImageUnit->pOrderTarget->pSprite;
        const int nAngle = AngleBetween(PosS(pUs->wPosX), PosS(pUs->wPosY), PosS(pThem->wPosX), PosS(pThem->wPosY));
        if ((DWORD)get_angle(nAngle, wAngle) < wSpread)
          pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      }
      break;
    }
    case 0x4A: {  // curdirectcondjmp
      const WORD wAngle = reinterpret_cast<const WORD *>(pScript)[0];
      const WORD wSpread = reinterpret_cast<const WORD *>(pScript)[1];
      const DWORD dwTarget = ScriptDword(pScript + 4);
      pScript += 8;
      if (!a2 && (DWORD)get_angle(gpImageUnit->bUnion21, wAngle) < wSpread)
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      break;
    }
    case 0x4B: {  // liftoffcondjmp
      const DWORD dwTarget = ScriptDword(pScript);
      pScript += 4;
      if (!a2 && (gpImageUnit->dwFlags & 4))
        pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      break;
    }
    case 0x4C: {  // call
      const DWORD dwTarget = ScriptDword(pScript);
      pScript += 4;
      pState->wUnk04 = (iscript_off_t)(pScript - reinterpret_cast<const BYTE *>(sgpScripts));
      pScript = reinterpret_cast<const BYTE *>(sgpScripts) + dwTarget;
      break;
    }
    case 0x4D: {  // useweapon
      const WORD wWeapon = *reinterpret_cast<const WORD *>(pScript);
      pScript += 2;
      if (!a2)
        gpImageUnit->FireWeapon(static_cast<weapon_id_t>(wWeapon));
      break;
    }
    case 0x37: {  // setflspeed
      pScript += 2;
      WORD wSpeed = reinterpret_cast<const WORD *>(pScript)[-1];
      if (!a2)
        gpImageUnit->lTopSpeed = wSpeed;
      break;
    }
    case 0x05: {  // wait
      pScript++;
      BYTE bWait = pScript[-1] - 1;
      pState->wIscriptOffset =
          (iscript_off_t)(pScript - reinterpret_cast<const BYTE *>(sgpScripts));
      pState->bWait = bWait;
      return;
    }
    }
  }
}

// Re-read the special overlay offset, then turn the image to an angle: 256 angle units map
// to 32 directions, the upper half mirrored. SetRLE's work is written out on the flags
// word, as in FlipImage. The angle is `((BYTE)nDirection + 4) >> 3` off the int slot.
void CImage::SetFacing(int nDirection)
{
  WORD wFlags = this->flags.w;

  if (wFlags & 0x80) {
    CImage *pMain = this->pParentSprite->pMainImage;
    const char *p = reinterpret_cast<const char *>(
        ImageOverlay(2, pMain->wImageID));  // Hyperion: sgpOverlayGroups[999 * 2 + ...]
    p += reinterpret_cast<const lo_file_t *>(p)->adwFrames[pMain->wFrameIndex];
    int nPosX = p[0];
    if (pMain->flags.w & 2)
      nPosX = -nPosX;
    int nPosY = p[1];
    if (this->cHorzOffset != (char)nPosX || this->cVertOffset != (char)nPosY) {
      wFlags |= 1;
      this->cHorzOffset = (char)nPosX;
      this->cVertOffset = (char)nPosY;
      this->flags.w = wFlags;
    }
  }

  wFlags = this->flags.w;

  if (!(wFlags & 8))
    return;

  BYTE bFlip;
  BYTE bDir = ImageFold(this, (BYTE)(((BYTE)nDirection + 4) >> 3), &bFlip);  // Hyperion: a 32's

  if (this->bDirection == bDir && ((wFlags >> 1) & 1) == bFlip)
    return;

  this->bDirection = bDir;
  wFlags = (WORD)(((bFlip & 1) * 2) | (wFlags & 0xFFFD));
  this->flags.w = wFlags;
  BYTE bRLE = this->bPaletteType;
  this->bPaletteType = bRLE;
  this->pfnUpdate    = sgUpdateFuncs[bRLE].pfnUpdate;
  if (wFlags & 2)
    this->pfnRender = sgDrawFuncs[bRLE].pfnFlipped;
  else
    this->pfnRender = sgDrawFuncs[bRLE].pfnNormal;
  if (bRLE == 0x11) {
    this->coloringData.rle.bRleFrame = 0x30;
    this->coloringData.rle.bRleWait  = 0x02;
  }
  this->flags.w = (WORD)(wFlags | 1);

  int nIdx = ImageFrame(this, this->wFrameSet, bDir);  // Hyperion: a 32's

  if (this->wFrameIndex != nIdx) {
    this->wFrameIndex = (WORD)nIdx;
    this->flags.w |= 1;
  }
}

// Returns whether the unit's owner is the local player; always false during
// replay playback.
BOOL IsSelfOwned(CUnit *pUnit) {
  if (gPlayback == 0 && pUnit->bPlayer == g_LocalNationID)
    return 1;
  return 0;
}

// The empty swap LoadImages and SaveImages call on each record lives in another TU, and
// /OPT:ICF folded it onto 0x4BE950; gen thunks this spelling to that address.
void __fastcall image_swap_nothing(CImage *pImage);

#pragma pack(push, 1)
struct image_saveentry_t {
  /*0x00*/ WORD idx;
  /*0x02*/ CImage image;
};
#pragma pack(pop)
OC_X86_LAYOUT(sizeof(image_saveentry_t) == 0x42, "(n << 5) + n, doubled");
static_assert(offsetof(image_saveentry_t, idx) == 0x00, "movzx eax, word ptr [edi]");
static_assert(offsetof(image_saveentry_t, image) == 0x02, "movdqu xmm0, [edi+2h]");

// 0x410090. Read the image pool back: a WORD record count, then the compressed records,
// each copied to the slot its index names; then decode the ids and rebuild the free list,
// whether or not anything was read. The record counter is unsigned 32-bit.
BOOL CImage::LoadImages(void *pFile, WORD wVersion) {
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(g_pools.images);
  DWORD wCount = 0;
  if (!pfread(&wCount, nSlotBytes, pFile))
    return FALSE;
  memset(sgImages, 0, sizeof(CImage) * g_pools.images);
  if (wCount != 0) {
    // Hyperion: a record is the index and the image in 1.17's layout,
    // image_saveentry_t on x86.
    DWORD nSize = wCount * (nSlotBytes + PortSave117Size(SAVE117_CImage));
    BYTE *pBuf = static_cast<BYTE *>(SMemAlloc(nSize, "CImage.cpp", 0x127C, 0));
    const BYTE *pRec = pBuf;
    memset(pBuf, 0, nSize);
    if (!decompress(pBuf, nSize, pFile)) {
      SMemFree(pBuf, "CImage.cpp", 0x1282, 0);
      return FALSE;
    }
    for (DWORD i = 0; i < wCount; i++) {
      const unsigned w = PoolSaveSlot(pRec, nSlotBytes);
      // Hyperion: a slot past the game's pool is a broken save
      if (w >= static_cast<unsigned>(g_pools.images)) {
        SMemFree(pBuf, "CImage.cpp", 0, 0);
        return FALSE;
      }
      pRec = PortSave117Get(SAVE117_CImage, pRec + nSlotBytes, &sgImages[w]);
      image_swap_nothing(&sgImages[w]);
    }
    SMemFree(pBuf, "CImage.cpp", 0x1294, 0);
  }
  DecodeListFromSave();
  sub_4103C0();
  return TRUE;
}

// 0x411300. Write the image pool: a WORD count of the slots in use, then those slots
// compressed, each behind its pool index and swapped around the copy.
BOOL CImage::SaveImages(void *pFile) {
  EncodeListForSave();
  DWORD dwCount = 0;
  for (DWORD i = 0; i < g_pools.images; i++)
    if (sgImages[i].pGroup != 0)
      dwCount++;
  // Hyperion: the count and each slot in 4 bytes for a pool past 65,535
  const unsigned nSlotBytes = PoolSaveBytes(g_pools.images);
  BOOL bOk = pfwrite(&dwCount, nSlotBytes, pFile);
  if (bOk && dwCount != 0) {
    DWORD nSize = dwCount * (nSlotBytes + PortSave117Size(SAVE117_CImage));  // Hyperion
    BYTE *pBuf = (BYTE *)SMemAlloc(nSize, "CImage.cpp", 0x124A, 0);
    BYTE *pRec = pBuf;
    memset(pBuf, 0, nSize);
    dwCount = 0;
    for (int i = 0; i < g_pools.images; i++) {
      CImage *p = &sgImages[i];
      if (p->pGroup != 0) {
        PoolSaveSlotPut(pRec, static_cast<unsigned>(i), nSlotBytes);
        pRec += nSlotBytes;
        image_swap_nothing(p);
        pRec = PortSave117Put(SAVE117_CImage, p, pRec);
        image_swap_nothing(p);
        dwCount++;
      }
    }
    bOk = PortSaveChunk(pBuf, nSize, dwCount, pFile);
    SMemFree(pBuf, "CImage.cpp", 0x125F, 0);
  }
  DecodeListFromSave();
  return bOk;
}

// Rebuilds the main image free list after a load from the empty sgImages slots.
void sub_4103C0() {
  int i;
  for (i = 0; i < g_pools.images && sgImages[i].pGroup != 0; i++)
    ;
  if (i == g_pools.images) {
    images_sgpFreeTail = 0;
    images_sgpFreeHead_g = 0;
    return;
  }
  CImage *pPrev = &sgImages[i];
  i++;
  images_sgpFreeTail = pPrev;
  images_sgpFreeHead_g = pPrev;
  for (; i < g_pools.images; i++) {
    if (sgImages[i].pGroup == 0) {
      reinterpret_cast<CLink *>(pPrev)->AppendLink(
          reinterpret_cast<CLink *>(&sgImages[i]),
          reinterpret_cast<CLink **>(&images_sgpFreeTail));
      pPrev = &sgImages[i];
    }
  }
}

// Sets or clears flags bit 7; setting it also re-reads the special overlay offset from the
// main image's .lo row, as ApplySpecialOffset does.
void CImage::sub_40F3A0(BYTE bEnable)
{
  if (((this->flags.w >> 7) & 1) == bEnable)
    return;
  this->flags.w = (WORD)(((bEnable & 1) << 7) | (this->flags.w & 0xFF7F));
  if (!bEnable)
    return;
  CImage *pMain = this->pParentSprite->pMainImage;
  const char *p = reinterpret_cast<const char *>(
      ImageOverlay(2, pMain->wImageID));  // Hyperion: sgpOverlayGroups[999 * 2 + ...]
  p += reinterpret_cast<const lo_file_t *>(p)->adwFrames[pMain->wFrameIndex];
  int nPosX = p[0];
  if (pMain->flags.w & 2)
    nPosX = -nPosX;
  int nPosY = p[1];
  if (this->cHorzOffset != (char)nPosX || this->cVertOffset != (char)nPosY) {
    this->cHorzOffset = (char)nPosX;
    this->cVertOffset = (char)nPosY;
    this->flags.w |= 1;
  }
}

// sprites.dat health bar width column, biased by 130 like the two selection
// circle columns above (HealthbarInit reads it at 0x51EE36 = 0x51EEB8 - 130).
#ifdef OC_DLL_MODE
#ifndef sprite_healthbarSize
#define sprite_healthbarSize (reinterpret_cast<BYTE *>(OC_VA(0x51EEB8u)))
#endif
#else
extern BYTE sprite_healthbarSize[387];
#endif

// Sizes this healthbar image's private GROUP from the sprite's sprites.dat
// width and its unit's shield/energy rows, then starts its script.
void CImage::HealthbarInit(CSprite *pSprite)
{
  CUnit *pUnit = NewUnits()[pSprite->bSelectionIndex];  // Hyperion: selectlimits.h's (stage S)
  BYTE bSize = sprite_healthbarSize[pSprite->wSpriteID - 130];
  this->cVertOffset = (char)(
      (sgpImgGroups[sprite_selCircleImg[pSprite->wSpriteID - 130] + 561]
           ->frames[0].bHeight >> 1) +
      8 + sprite_selCircleOffset[pSprite->wSpriteID - 130]);
  this->pGroup->frames[0].bOffsetX = 0;
  this->pGroup->frames[0]._unmapped_01 = 0;
  this->pGroup->frames[0].bWidth = (BYTE)(bSize - (bSize - 1) % 3);
  if (this->pGroup->frames[0].bWidth < 19)
    this->pGroup->frames[0].bWidth = 19;
  this->pGroup->frames[0].bHeight = 0;
  if ((gulUnitTypeFlags[pUnit->wType] & 0x200000 && !(pUnit->dwFlags & 0x40000000)) ||
      (pUnit->dwFlags & 0x40000000) || pUnit->wType == 0x28)
    this->pGroup->frames[0].bHeight += 6;
  if (gbUnitHasShields[pUnit->wType])
    this->pGroup->frames[0].bHeight += 7;
  else
    this->pGroup->frames[0].bHeight += 5;
  this->pGroup->wMaxWidth = this->pGroup->frames[0].bWidth;
  this->pGroup->wMaxHeight = this->pGroup->frames[0].bHeight;
  this->flags.w = 0;
  this->pParentSprite = pSprite;
  memset(&this->wGrpX, 0, 8);
  this->coloringData.dw = reinterpret_cast<DWORD_PTR>(pUnit);
  this->DrawUpdateRect();
  iscript_off_t *pIscript = &this->wIscriptHeader;
  const iscript_off_t anim = ImageScriptAnim(*pIscript, 0);  // Hyperion
  this->flags.b.Dirty = 1;
  this->bAnim = 0;
  this->wIscriptOffset = anim;
  this->bWait = 0;
  this->w14 = 0;
  this->RunScript(pIscript, 0, 0);
}

// images.dat "draw if cloaked" column, sheet name.
#ifdef OC_DLL_MODE
#ifndef img_drawIfCloaked
#define img_drawIfCloaked (reinterpret_cast<BYTE *>(OC_VA(0x5132A0u)))
#endif
#else
extern BYTE img_drawIfCloaked[999];
#endif

// Allocates an overlay image linked above or below this one in its sprite,
// inheriting the hallucination palette, facing and cloak state of gpImageUnit.
CImage *CImage::CreateOverlay(int nImageID, CSprite *pSprite, BYTE x, BYTE y,
                              BOOL bAbove)
{
  CImage *pImage = CImage::AllocImage();
  if (!pImage)
    return 0;
  if (bAbove)
    reinterpret_cast<CLink *>(this)->PrependLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pParentSprite->pImageHead));
  else
    reinterpret_cast<CLink *>(this)->AppendLink(
        reinterpret_cast<CLink *>(pImage),
        reinterpret_cast<CLink **>(&this->pParentSprite->pImageTail));
  pImage->Initialize(nImageID, this->pParentSprite, x, y);
  pImage->InitScript();
  if (pImage->bPaletteType == 0 && gpImageUnit &&
      (gpImageUnit->dwFlags & 0x40000000) &&
      (gPlayback || gpImageUnit->bPlayer == g_LocalNationID)) {
    pImage->SetRLE(0x10);
    pImage->coloringData.dw = 0;
  }
  if (pImage->flags.b.GfxTurns)
    pImage->FaceDirection(this->flags.b.Mirror ? 0x20 - this->bDirection
                                                : this->bDirection);
  pImage->CalcDisplayNo();
  if (gpImageUnit && (gpImageUnit->dwFlags & 0x300)) {
    if (!img_drawIfCloaked[nImageID])
      pImage->Hide();
    else if (pImage->bPaletteType == 0)
      pImage->InheritDrawInfo(this->pParentSprite->pMainImage);
  }
  return pImage;
}

// LoadGroups' globals, sheet names (main.cpp, gamedata.cpp, arr/images.cpp).
#ifndef OC_SGBLOWMEM_G
#define OC_SGBLOWMEM_G
#ifdef OC_DLL_MODE
#define sgbLowMem_g (*reinterpret_cast<DWORD *>(OC_VA(0x66FD30u)))
#else
extern DWORD sgbLowMem_g;
#endif
#endif
#ifdef OC_DLL_MODE
#ifndef sgbLoadLowMemFile_g
#define sgbLoadLowMemFile_g (*reinterpret_cast<DWORD *>(OC_VA(0x6662C4u)))
#endif
#ifndef img_grp
#define img_grp (reinterpret_cast<DWORD *>(OC_VA(0x511748u)))
#endif
#ifndef img_shieldOverlay
#define img_shieldOverlay (reinterpret_cast<DWORD *>(OC_VA(0x514DF8u)))
#endif
#ifndef img_atkOverlay
#define img_atkOverlay (reinterpret_cast<DWORD *>(OC_VA(0x515D98u)))
#endif
#ifndef img_dmgOverlay
#define img_dmgOverlay (reinterpret_cast<DWORD *>(OC_VA(0x516D38u)))
#endif
#ifndef img_specialOverlay
#define img_specialOverlay (reinterpret_cast<DWORD *>(OC_VA(0x517CD8u)))
#endif
#else
extern DWORD sgbLoadLowMemFile_g;
extern DWORD img_grp[999];
extern DWORD img_shieldOverlay[999];
extern DWORD img_atkOverlay[999];
extern DWORD img_dmgOverlay[999];
extern DWORD img_specialOverlay[999];
#endif

// Loads images.tbl, every image GRP (in low-memory file mode when the game
// runs low-memory), then the five .lo overlay rows and the shield overlays.
void LoadGroups()
{
  grhreq_t aReq[999];
  intptr_t anSizes[999];
  void *pStr = reinterpret_cast<void *>(
      str_load_table(reinterpret_cast<intptr_t>("arr\\images.tbl"),
                     reinterpret_cast<intptr_t>(load_game_data),
                     reinterpret_cast<intptr_t>("CImage.cpp"), 0x72E));
  sgbLoadLowMemFile_g = sgbLowMem_g;
  group_load_tbl(reinterpret_cast<void **>(sgpImgGroups), img_grp, pStr, 999,
                 aReq, anSizes);
  sgbLoadLowMemFile_g = 0;
  group_load_lo(reinterpret_cast<void **>(&sgpOverlayGroups[999 * 0]),
                img_atkOverlay, pStr, 999, aReq, anSizes);
  group_load_lo(reinterpret_cast<void **>(&sgpOverlayGroups[999 * 1]),
                img_dmgOverlay, pStr, 999, aReq, anSizes);
  group_load_lo(reinterpret_cast<void **>(&sgpOverlayGroups[999 * 2]),
                img_specialOverlay, pStr, 999, aReq, anSizes);
  group_load_lo(reinterpret_cast<void **>(&sgpOverlayGroups[999 * 3]),
                img_landingDustOverlay, pStr, 999, aReq, anSizes);
  group_load_lo(reinterpret_cast<void **>(&sgpOverlayGroups[999 * 4]),
                img_liftoffDustOverlay, pStr, 999, aReq, anSizes);
  group_load_lo(const_cast<void **>(reinterpret_cast<const void **>(sgpShieldOverlayGroups_g)),
                img_shieldOverlay, pStr, 999, aReq, anSizes);
  mem_free(pStr, "CImage.cpp", 0x749);
}

// InitImages' pools and the tileset name table, sheet names (CImage.cpp, gamemap.cpp).
#ifdef OC_DLL_MODE
#ifndef sgHpbarImages
#define sgHpbarImages (reinterpret_cast<CImage *>(OC_VA(0x5928A0u)))
#endif
#ifndef sgCircleImages_g
#define sgCircleImages_g (reinterpret_cast<CImage *>(OC_VA(0x592C50u)))
#endif
#ifndef sgPlacemaskGroups_g
#define sgPlacemaskGroups_g (reinterpret_cast<GROUP *>(OC_VA(0x596080u)))
#endif
#ifndef gpszTilesetNames_g
#define gpszTilesetNames_g (reinterpret_cast<const char **>(OC_VA(0x503398u)))
#endif
#else
extern CImage sgHpbarImages[SELECTION_MAX];
extern CImage sgCircleImages_g[80] OC_UNALIGNED;
extern GROUP sgPlacemaskGroups_g[64];
extern const char *gpszTilesetNames_g[8] OC_UNALIGNED;
#endif
#ifdef OC_DLL_MODE
#ifndef g_MapTileset
#define g_MapTileset (*reinterpret_cast<WORD *>(OC_VA(0x5264BCu)))
#endif
#else
extern WORD g_MapTileset;
#endif

// Seats a placemask pool entry on its own one-frame GROUP.
static void init_placemask_image(CImage *pImage, GROUP *pGroup)
{
  pImage->wImageID = 0x247;
  pImage->pGroup = pGroup;
  *reinterpret_cast<WORD *>(pImage->pGroup) = 1;
  pImage->flags.w &= 0xFFF7;
  pImage->flags.w &= 0xFFDF;
  pImage->flags.w &= 0xFFEF;
  pImage->wFrameSet = 0;
  pImage->wFrameIndex = 0;
  pImage->bDirection = 0;
  pImage->SetRLE(0x0F);
}

// Clears the five image pools, threads each onto its free list, and loads the
// tileset's shift table. The placemask helper must store wImageID before
// pGroup and clear the three flag bits as three separate ANDs.
void CImage::InitImages()
{
  char szPath[0x104];
  memset(sgImages, 0, sizeof(CImage) * g_pools.images);
  memset(sgHpbarImages, 0, sizeof(CImage) * SELECTION_MAX);
  memset(sgCircleImages_g, 0, sizeof(CImage) * 80);
  memset(sgPlaceboxImages_g, 0, sizeof(CImage) * 64);
  memset(sgPlacemaskImages_g, 0, sizeof(CImage) * 64);

  hpbar_images_sgpFreeTail_g = &sgHpbarImages[0];
  hpbar_images_sgpFreeHead = &sgHpbarImages[0];
  sgHpbarImages[0].InitHPBar(0);
  for (DWORD i = 1; i < SELECTION_MAX; i++) {
    reinterpret_cast<CLink *>(&sgHpbarImages[0])->AppendLink(
        reinterpret_cast<CLink *>(&sgHpbarImages[i]),
        reinterpret_cast<CLink **>(&hpbar_images_sgpFreeTail_g));
    sgHpbarImages[i].InitHPBar(i);
  }

  circle_images_sgpFreeTail_g = &sgCircleImages_g[0];
  circle_images_sgpFreeHead_g = &sgCircleImages_g[0];
  for (DWORD i = 1; i < 80; i++)
    reinterpret_cast<CLink *>(&sgCircleImages_g[0])->AppendLink(
        reinterpret_cast<CLink *>(&sgCircleImages_g[i]),
        reinterpret_cast<CLink **>(&circle_images_sgpFreeTail_g));

  placebox_images_sgpFreeTail_g = &sgPlaceboxImages_g[0];
  placebox_images_sgpFreeHead_g = &sgPlaceboxImages_g[0];
  for (DWORD i = 1; i < 64; i++)
    reinterpret_cast<CLink *>(&sgPlaceboxImages_g[0])->AppendLink(
        reinterpret_cast<CLink *>(&sgPlaceboxImages_g[i]),
        reinterpret_cast<CLink **>(&placebox_images_sgpFreeTail_g));

  placemask_images_sgpFreeTail_g = &sgPlacemaskImages_g[0];
  placemask_images_sgpFreeHead_g = &sgPlacemaskImages_g[0];
  init_placemask_image(&sgPlacemaskImages_g[0], &sgPlacemaskGroups_g[0]);
    // A WORD index; a DWORD one becomes an end-pointer loop.
  for (WORD i = 1; i < 64; i++) {
    reinterpret_cast<CLink *>(&sgPlacemaskImages_g[0])->AppendLink(
        reinterpret_cast<CLink *>(&sgPlacemaskImages_g[i]),
        reinterpret_cast<CLink **>(&placemask_images_sgpFreeTail_g));
    init_placemask_image(&sgPlacemaskImages_g[i], &sgPlacemaskGroups_g[i]);
  }

  images_sgpFreeTail = &sgImages[0];
  images_sgpFreeHead_g = &sgImages[0];
  for (DWORD i = 1; i < g_pools.images; i++)
    reinterpret_cast<CLink *>(&sgImages[0])->AppendLink(
        reinterpret_cast<CLink *>(&sgImages[i]),
        reinterpret_cast<CLink **>(&images_sgpFreeTail));

  psnprintf(szPath, 0x104, "Tileset\\%s\\shift.pcx",
            gpszTilesetNames_g[g_MapTileset]);
  GameLoadImage(reinterpret_cast<intptr_t>(szPath), 0,
                reinterpret_cast<intptr_t>(sgShiftImg_g), 0x100, 0, 0, 0);
}

// Hyperion: a game of Hyperion's selection (stage S): the health bars and circles from
// pools for 255 units, threaded as InitImages threads 1.17's. As the selection starts no image
// of 1.17's two pools is out, and none is again in this game.
void CImage::InitSelectionImagesWide()
{
  memset(g_hxHpbarImages, 0, sizeof g_hxHpbarImages);
  memset(g_hxCircleImages, 0, sizeof g_hxCircleImages);

  hpbar_images_sgpFreeTail_g = &g_hxHpbarImages[0];
  hpbar_images_sgpFreeHead = &g_hxHpbarImages[0];
  g_hxHpbarImages[0].InitHPBar(0);
  for (DWORD i = 1; i < SELECTION_WIDE; i++) {
    reinterpret_cast<CLink *>(&g_hxHpbarImages[0])->AppendLink(
        reinterpret_cast<CLink *>(&g_hxHpbarImages[i]),
        reinterpret_cast<CLink **>(&hpbar_images_sgpFreeTail_g));
    g_hxHpbarImages[i].InitHPBar(i);
  }

  circle_images_sgpFreeTail_g = &g_hxCircleImages[0];
  circle_images_sgpFreeHead_g = &g_hxCircleImages[0];
  for (DWORD i = 1; i < SELECTION_CIRCLES_WIDE; i++)
    reinterpret_cast<CLink *>(&g_hxCircleImages[0])->AppendLink(
        reinterpret_cast<CLink *>(&g_hxCircleImages[i]),
        reinterpret_cast<CLink **>(&circle_images_sgpFreeTail_g));
}
