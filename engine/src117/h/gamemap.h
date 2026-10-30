// gamemap.h -- declarations for src117/lang/gamemap.cpp: the playfield surface
// and the click routing that turns a screen point back into a map target.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GAMEMAP_H
#define OC117_GAMEMAP_H

#include "platform.h"

// ECX then EDX; it tail-jumps cell_set_pos.
void gamemap_center_on_mtx(int nTileX, int nTileY);

void gamemap_force_redraw();

// It tail-jumps CSprite::PriorityAddRange.
void gamemap_priority_update();

void gamemap_choose_center();

void gamemap_gen_default();

// maphdr_MTXM uses the int result.
int __fastcall convert_cellmap(void);

// It tail-jumps light_free.
void gamemap_free();

// 0x445A30, 830 B.
void gamemap_init();

void redraw_dirty_mtx();

// Callees in other TUs that no other header declares.

// repulse_free (0x4816C0), scroll_free (0x49B7D0), colorcycling_free (0x466760)
// and light_free (0x466790) are nullary, called from gamemap_free.
//
// cell_copy_scrollbuf_row -- 0x405AAD, hand-written assembly in cellcopy.asm:
// ECX, EDX and two stack dwords, which it pops itself (`ret 8h`).
void cell_copy_scrollbuf_row(int nRow, int nX, int nWidth, int nOfs);

// 0x40B220 cell_refresh, 0x405A43 cell_copy_scrollbuf (assembly, no prologue),
// 0x49B900 DrawStars and 0x49BA80 DrawStarsDirty: nullary, called only by
// gamemap_update.
void cell_refresh();
void cell_copy_scrollbuf();
void DrawStars();
void DrawStarsDirty();

// 0x49BEF0 scroll_reset, 0x49B7F0 scroll_init_game and 0x4B44A0 sub_4B44A0:
// nullary, called only by gamemap_init.
void scroll_reset();
void scroll_init_game();
void sub_4B44A0();

// 0x466810 and 0x4668F0, light.cpp: each loads a tileset .pcx named by
// pszTileset. Neither reads pPalette, but gamemap_init passes it.
void sub_466810(BYTE *pPalette, const char *pszTileset);
void sub_4668F0(BYTE *pPalette, const char *pszTileset);

// 0x4B4040, 0x4B45F0 and 0x4B4560, tile2.cpp: the three creep_hooks_t slots
// gamemap_init fills, spelled as creep_hooks_t declares them (__stdcall).
void __stdcall sub_4B4040(int nX, int nY, int nParam, WORD nTile, int *pBox);
BOOL __stdcall tile_covered_by_creep(int nX, int nY);
BOOL __stdcall tile_is_fogged(int nX, int nY);

void gamemap_init();

void scroll_free();
void colorcycling_free();
void light_free();

// The playfield's paint callback, which gamemap_reinit stores into _img_list[5]
// +0x10. 1.17's is __stdcall with two stack slots it never reads (`ret 8h`);
// the engine spells it as img.h's PFN_IMG_UPDATE, the four parameters
// img_update_real passes.
struct bmp_t;
struct bmpsrc_t;
void __fastcall gamemap_update(unsigned short nX, unsigned short nY, bmp_t *pBmp, bmpsrc_t *pSrc);

void gamemap_reinit();

// pPal points at 256 four-byte palette entries, walked backwards from +0x3FC.
void gray_pal_init(BYTE *pPal);

#endif  // OC117_GAMEMAP_H
