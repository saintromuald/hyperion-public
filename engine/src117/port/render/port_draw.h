// port_draw.h -- the map's tiles, the sprite routines and the bitmap copies, in the original's
// conventions (port_draw.cpp), which the game reaches by the original's names (port_names.cpp).
#ifndef PORT_DRAW_H
#define PORT_DRAW_H

#include "../../h/platform.h"  // LONG, ULONG and the pointer-sized types

// The map's tile drawing (port_draw.cpp), which the game reaches by the original's
// names (port_names.cpp), in the original's conventions: draw_cell (0x405AF0),
// rle_creep (0x405D40), cell_copy_scrollbuf (0x405A43) and
// cell_copy_scrollbuf_row (0x405AAD, callee pops 8).
void __fastcall PortDrawCell(int nCell, int nDest);
void __fastcall PortRleCreep(const unsigned char *pRecord, int nDest);
void PortCellCopyScrollbuf(void);
void __fastcall PortCellCopyScrollbufRow(int nRow, int nX, int nWidth, int nOfs);

// The distortion routines (port_draw.cpp), draw functions 3 and 8 of the
// image table at 0x5030D0, plain and mirrored: rle_draw_invis_asm (0x4063B5),
// rle_draw_invis_hflip_asm (0x406C09), rle_draw_emp_asm and rle_draw_emp_hflip_asm. The
// original's convention: x and y in ECX and EDX, the frame, the clip and an
// unused word on the stack, callee pops 12.
void __fastcall PortRleDrawInvis(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwUnused);
void __fastcall PortRleDrawInvisHflip(int nX, int nY, const void *pGraphic, const void *pClip,
                                      ULONG_PTR dwUnused);
void __fastcall PortRleDrawEmp(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwUnused);
void __fastcall PortRleDrawEmpHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwUnused);

// The bitmap copies' clip and copies (port_draw.cpp, R2b), in the original's
// conventions: img_clipped (0x4CFE60, the source block in ECX and the
// destination block in EDX), bmp_copy_rect_asm (0x4CFF90) and
// bmp_mask_rect_asm (0x4CFFD7), which take the blit globals at 0x50F2F0. The
// two copies save every register in the original; their callers are C++.
struct bmpsrc_t;
int __fastcall PortImgClipped(struct bmpsrc_t *pSrc, struct bmpsrc_t *pDst);
void PortBmpCopyRect(void);
void PortBmpMaskRect(void);
// draw_char (0x4CFDF0, port_draw.cpp): the destination in ECX, the glyph's
// bits in EDX. The original saves EAX as well; font_puts, its only caller, is
// C++.
void __fastcall PortDrawChar(unsigned char *pDest, const unsigned char *pBits);
// shade_mtx (0x405C12, port_draw.cpp): a cell of the placement grid; x and y
// in ECX and EDX, the bitmap and the pen on the stack, callee pops 8.
void __fastcall PortShadeMtx(int nX, int nY, const void *pBitmap, ULONG dwPen);
// The other sprite routines (port_draw.cpp, R2b), draw functions of the image table at
// 0x5030D0, plain and mirrored, in the distortion routines' convention: rle_draw_normal and
// rle_draw_hflip_, rle_draw_shadow, _effect, _fade and _uncloaked_part (each with _hflip) and
// rle_mask_warp_flash(_hflip). dwArg, the draw function's table, texture or threshold, is
// pointer-sized: a table's address for some, and the image table holds them under one type.
void __fastcall PortRleDrawNormal(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawNormalHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawShadow(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawShadowHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawEffect(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawEffectHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawFade(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawFadeHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawUncloakedPart(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleDrawUncloakedPartHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleMaskWarpFlash(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);
void __fastcall PortRleMaskWarpFlashHflip(int nX, int nY, const void *pGraphic, const void *pClip, ULONG_PTR dwArg);

#endif
