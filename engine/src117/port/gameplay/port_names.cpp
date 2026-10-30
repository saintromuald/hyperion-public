// port_names.cpp -- the game's names for functions whose bodies are the port's.
//
// The game's C++ calls these by 1.17's names and prototypes, and the port's bodies
// (port_draw.cpp, port_veclen.cpp) have prototypes of their own: const where they
// only read, a pointer-wide integer where the argument is a value or a pointer by
// the routine. Each name here passes its arguments on as they are, which is what the
// generated thunks did at 32 bits.
#include <windows.h>
#include "CImage.h"
#include "COrder.h"
#include "CThingy.h"
#include "CUnit.h"
#include "CTarget.h"
#include "cellscrl.h"
#include "gamemap.h"
#include "gds_bmpcopre.h"
#include "gds_bmpmasre.h"
#include "gds_font.h"
#include "placebox.h"
#include "rtti.h"
#include "TrigAct.h"
#include "../render/port_draw.h"
#include "port_veclen.h"  // PortVeclen, PortVeclenPrecise

// The map's tiles.
void cell_copy_scrollbuf() { PortCellCopyScrollbuf(); }
void cell_copy_scrollbuf_row(int nRow, int nX, int nWidth, int nOfs)
{
  PortCellCopyScrollbufRow(nRow, nX, nWidth, nOfs);
}
void draw_cell(int nCell, int nDest) { PortDrawCell(nCell, nDest); }
void shade_mtx(int nArg0, int nArg1, void *pArg2, int nArg3)
{
  PortShadeMtx(nArg0, nArg1, pArg2, static_cast<ULONG>(nArg3));
}

// creep.cpp's own declaration; its one caller does not read the result.
int __fastcall rle_creep(BYTE *pEdgeRecord, int nParam);
int __fastcall rle_creep(BYTE *pEdgeRecord, int nParam)
{
  PortRleCreep(pEdgeRecord, nParam);
  return 0;
}

// The distance routines.
extern "C" int __fastcall veclen(int ax, int bx, int ay, int by) { return PortVeclen(ax, bx, ay, by); }
extern "C" int __fastcall veclen_precise(int ax, int bx, int ay, int by)
{
  return PortVeclenPrecise(ax, bx, ay, by);
}

// The sprite routines. The last argument is a remapping table, a value or unused,
// as the routine reads it.
#define RLE_FORWARD(name, port, Graphic, Clip, Last)                                         \
  void __fastcall name(int nX, int nY, Graphic pGraphic, Clip pClip, Last last)              \
  {                                                                                          \
    port(nX, nY, pGraphic, pClip, (ULONG_PTR)last);                                          \
  }
RLE_FORWARD(rle_draw_normal, PortRleDrawNormal, void *, void *, BYTE *)
RLE_FORWARD(rle_draw_hflip_, PortRleDrawNormalHflip, void *, void *, BYTE *)
RLE_FORWARD(rle_mask_warp_flash, PortRleMaskWarpFlash, void *, void *, BYTE *)
RLE_FORWARD(rle_mask_warp_flash_hflip, PortRleMaskWarpFlashHflip, void *, void *, BYTE *)
RLE_FORWARD(rle_draw_uncloaked_part, PortRleDrawUncloakedPart, void *, void *, DWORD)
RLE_FORWARD(rle_draw_uncloaked_part_hflip, PortRleDrawUncloakedPartHflip, void *, void *, DWORD)
RLE_FORWARD(rle_draw_invis_asm, PortRleDrawInvis, void *, void *, DWORD)
RLE_FORWARD(rle_draw_invis_hflip_asm, PortRleDrawInvisHflip, void *, void *, DWORD)
RLE_FORWARD(rle_draw_effect, PortRleDrawEffect, void *, void *, void *)
RLE_FORWARD(rle_draw_effect_hflip, PortRleDrawEffectHflip, void *, void *, void *)
RLE_FORWARD(rle_draw_fade, PortRleDrawFade, FRAME *, rle_clip_t *, BYTE *)
RLE_FORWARD(rle_draw_fade_hflip, PortRleDrawFadeHflip, FRAME *, rle_clip_t *, BYTE *)
#undef RLE_FORWARD

// Text and bitmaps.
void draw_char(BYTE *pDest, const BYTE *pbBits) { PortDrawChar(pDest, pbBits); }
int img_clipped(bmpsrc_t *pSrc, bmpsrc_t *pDst) { return PortImgClipped(pSrc, pDst); }
void bmp_mask_rect_asm(void) { PortBmpMaskRect(); }

// gluMap.cpp's view of act_none, with the argument it passes and act_none ignores.
int __fastcall act_none(DWORD dwArg);
int __fastcall act_none(DWORD) { return act_none(); }

// Functions of 1.17's with nothing in them, which its linker folded onto one `ret`
// (voice_squelch's, vpacket.cpp), and type_info::raw_name, `lea eax,[ecx+8]; ret`,
// onto which it folded the game's accessors of that shape, with rtti.h's signature.
const char *type_info::raw_name() { return reinterpret_cast<const char *>(this) + 8; }
void __fastcall nullsub_1() {}
void __fastcall voice_squelch() {}
void __fastcall voice_squelch(DWORD) {}
void __fastcall voice_squelch(void *) {}
void __fastcall voice_squelch(CUnit *) {}
void __fastcall image_swap_nothing(CImage *) {}
void __fastcall thingy_swap_nothing(CThingy *) {}
void __fastcall nullsub_folded_with_voice_squelch(DWORD, BOOL) {}
extern "C" void __cdecl voice_squelch(int, const char *) {}
void COrder::PreSave() {}
void COrder::PostLoad() {}
