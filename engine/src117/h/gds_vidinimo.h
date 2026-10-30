// gds_vidinimo.h -- declarations for src117/lang/gds_vidinimo.cpp: video mode
// setup and teardown, gamma, and the path from a palette to the monitor. Free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_GDS_VIDINIMO_H
#define OC117_GDS_VIDINIMO_H

#include "platform.h"
#include "gds_palette.h"  // palentry_t

// apply_gamma_palette -- 0x4CF760. ECX is the destination and EDX the source
// (the memcpy argument order), one stack word the entry count, `ret 4h`. EAX
// at the ret is inherited and its one caller discards it.
void apply_gamma_palette(palentry_t *pDst, const palentry_t *pSrc, int nCount);

// get_gamma -- 0x4CF870. Nullary; all four callers take the full EAX, so int.
int get_gamma();

// vid_init_mode -- 0x4CF880. Nullary; its one caller discards EAX.
void vid_init_mode();

// palette_to_monitor -- 0x4CF8C0. `ret 4h`: ECX is the first entry written, EDX
// its palette index, the stack word the count. The explicit __fastcall is the
// /Gr default anyway.
void __fastcall palette_to_monitor(palentry_t *pEntries, int nFirst, int nCount);

// deinit_video -- 0x4CF900. Nullary; reached only through init.cpp's tail jmp.
void deinit_video();

// set_gamma -- 0x4CF930. ECX only, compared unsigned (`jbe`, `cmovb`); all
// three callers discard EAX.
void set_gamma(DWORD dwGamma);

#endif  // OC117_GDS_VIDINIMO_H
