// gluBNIcon.h -- declarations for src117/lang/gluBNIcon.cpp, the battle.net
// list icons.

#ifndef OC117_GLUBNICON_H
#define OC117_GLUBNICON_H

#include "platform.h"

// 0x44DFB0. ECX the window, EDX the x, then y and the icon id on the stack
// (`ret 8h`). All three callers discard EAX.
void sub_44DFB0(void *hWnd, int nX, int nY, int nIcon);

struct blt_size_t;
struct blt_rect_t;

// 0x44DEB0 (126 B). ECX the out rectangle, EDX the icon id; the source cell of
// that icon in the icon strip, clipped to the strip.
void sub_44DEB0(blt_rect_t *prc, unsigned int nIcon);

// 0x44DF30 (35 B) / 0x44E000 (88 B). Nullary; both callers of each discard EAX.
void bn_icons_free();
void bn_icons_init();

// 0x44DF60. ECX the destination surface, EDX its rectangle, then its size,
// its pitch and the icon id (`ret 0Ch`); blits the icon strip into it. The one
// caller discards EAX.
void sub_44DF60(void *pDest, blt_rect_t *prcDst, blt_size_t *pSizeDst,
                int nPitch, int nIcon);

// 0x44DFF0. Returns the fixed 0x5034C0 whatever ECX says; all 17 callers set
// ECX to an icon id and read [eax] / [eax+4h] as a width and a height.
blt_size_t *bn_icons_get_size(int nIcon);

#endif
