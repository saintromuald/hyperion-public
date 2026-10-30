// cellscrl.h -- declarations for src117/lang/cellscrl.cpp: the tiled map view.
// It owns the scroll position in pixels and in tiles, the map-edge limits, and
// the repaint of the cell grid when the view moves. Free functions are
// __fastcall through /Gr and are declared without the keyword.
//
// Four of its functions are declared elsewhere: cell_fog_update in gameloop.h,
// cell_init in game.h, cell_refresh in gamemap.h, cell_set_pos in minimap.h.

#ifndef OC117_CELLSCRL_H
#define OC117_CELLSCRL_H

#include "platform.h"

// 0x40AA90 (171 B). Nullary; it ends in a tail jump to gamemap_force_redraw.
void cell_draw_background();

// 0x405AF0, in 1.17's hand-written assembly block, with no TU of its own; the
// port defines it in port_names.cpp. Its callers, all in cell_draw_mtx,
// zero-extend a WORD into ECX, so the first parameter is int; none reads EAX.
void draw_cell(int nCell, int nDest);

// 0x40AB40 (212 B) and 0x40AE50 (219 B): ECX, EDX and one stack word. ECX is
// a WORD element address, `gfpCellMap + 2 * (row * gwMapWidth + col)`.
void cell_draw_col(WORD *pwCell, int nArg1, int nArg2);
void cell_draw_row(WORD *pwCell, int nArg1, int nArg2);

// 0x40AC20 and 0x40AD40: ECX, EDX and two stack words each; no caller reads
// EAX.
void cell_draw_mtx(WORD wMtx, int nDest, int nX, int nY);
void cell_draw_rect(int nArg0, int nArg1, int nArg2, int nArg3);

// 0x40B0E0 (312 B). Nullary; no caller reads EAX.
void cell_paint();

// 0x40B310 (315 B): ECX, EDX and three stack words.
void cell_refresh_row(WORD *pwCell, WORD *pwMI, DWORD *pdwSquare, int nRow,
                      int nOfs);

// 0x40B4D0 (259 B): ECX, EDX and two stack words, all four `int *`; the body
// reads and writes through each.
int cell_clip_rect(int *pnX, int *pnY, int *pnW, int *pnH);

// 0x40B5E0, 0x40B6C0, 0x40B790, 0x40B870. ECX is a pixel delta. Every path
// returns an explicit 1 or 0, so all four are BOOL.
BOOL cell_scroll_dn(int nPixels);
BOOL cell_scroll_lft(int nPixels);
BOOL cell_scroll_rgt(int nPixels);
BOOL cell_scroll_up(int nPixels);

#endif  // OC117_CELLSCRL_H
