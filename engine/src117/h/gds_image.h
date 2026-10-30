// gds_image.h -- declarations for src117/lang/gds_image.cpp, the screen-image
// layer: the eight overlay images composited onto the draw buffer, the
// 16-pixel dirty-tile map that decides what is redrawn, and the cursor
// save/restore around them.
//
// Free functions are __fastcall through /Gr and are declared without the
// keyword. img_area_invalid and img_area_is_invalid (dlg_dlgdraw.h), img_invalid
// and img_force (img.h) and img_cursor_invalid (cur.h) are declared in those
// headers, not here.

#ifndef OC117_GDS_IMAGE_H
#define OC117_GDS_IMAGE_H

#include "platform.h"
#include "bmp.h"
#include "rect32.h"

// 0x4CAE30 img_free (35 B). Nullary, plain `ret`. Releases the cursor save
// buffer img_init allocated and nulls it.
void img_free(void);

// 0x4CAE60 img_init (85 B). Nullary, plain `ret`. Clears the image table and
// the dirty-tile map, sizes the draw buffer at 640 by 480 with no pixels yet,
// and allocates the 0x900-byte cursor save block.
void img_init(void);

// 0x4CAF10 img_update_real (415 B). Nullary, plain `ret`; its one caller
// discards EAX. Composites every in-use image into the draw buffer and blits
// the dirty tiles to the surface.
void img_update_real(void);

// 0x4CB0B0 restore_cursor_area (135 B) / 0x4CB140 save_behind_cursor (190 B).
// Both `ret 4h`: ECX is the bitmap to copy through and the offset pair is EDX
// plus one stack word. save_behind_cursor subtracts them from the cursor
// image's position; restore_cursor_area reads neither, so its two offsets are
// named for the caller's shape, the same at both of its call sites.
void restore_cursor_area(bmp_t *pBmp, int nDX, int nDY);
void save_behind_cursor(bmp_t *pBmp, int nDX, int nDY);

// vid_copy_rect and vid_draw_mask, which img_update_real calls, are declared in
// gds_vidblit.h and defined in gds_vidblit.cpp. vid_copy_rect has to stay in
// that TU: with 114 bytes and one caller, /Ob2 would inline it here.

#endif  // OC117_GDS_IMAGE_H
