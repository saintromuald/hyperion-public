// gds_vidblit.h -- declarations for src117/lang/gds_vidblit.cpp, the screen
// blit layer: the locked-surface copy, the invalid-region flag, and the list of
// transparency masks that are punched out of the frame before it is shown.
//
// The TU's name is 1.17's own: vid_erase_mask's SMemFree owner tag is the
// .rdata string "vidblit.cpp" at 0x4F1330.
//
// create_mask returns a BLTMASK * and vid_erase_mask takes one, but status.cpp
// declares both with an integer there and keeps the handle in an integer
// global; retyping them moves status.cpp, this header and sgConsoleArtMask_g
// together. HSTRANS is typedef'd below rather than taken from Storm.h, which
// includes <windows.h> (gds_log.h says why that matters).

#ifndef OC117_GDS_VIDBLIT_H
#define OC117_GDS_VIDBLIT_H

#include "platform.h"
#include "bmp.h"
#include "rect32.h"
#include "TSList.h"

// Storm's transparency-mask handle, opaque here exactly as in Storm.h. A
// repeated identical typedef is legal C++, so both spellings can coexist.
typedef struct _HSTRANS *HSTRANS;

// The block vid_lock_surface fills with the surface's bits and pitch, 16 bytes
// in effects_draw_surface's frame. dlg_effects.h has an identical copy, which
// has to go before the two headers meet in gen/hook_table.cpp.
struct vidlock_t {
  /*0x00*/ void *pUnk0;
  /*0x04*/ void *pUnk4;
  /*0x08*/ int nPitch;
  /*0x0C*/ BYTE *pBits;
};
static_assert(offsetof(vidlock_t, pUnk0) == 0x00, "vidlock_t::pUnk0  vid_lock_surface: the object's own address is its first member");
OC_X86_LAYOUT(offsetof(vidlock_t, pUnk4) == 0x04, "vidlock_t::pUnk4  unread here; the slot is spanned by the 16-byte frame object");
OC_X86_LAYOUT(sizeof(vidlock_t) == 0x10, "vidlock_t size    effects_draw_surface: lea ecx, [ebp-1Ch] with [ebp-0Ch] taken");
OC_X86_LAYOUT(offsetof(vidlock_t, nPitch) == 0x08, "vidlock_t::nPitch vid_lock_surface: mov dword ptr [esi+8h], ecx");
OC_X86_LAYOUT(offsetof(vidlock_t, pBits) == 0x0C, "vidlock_t::pBits  vid_lock_surface: lea eax, dword ptr [esi+0Ch]");

// One entry of the mask list: the link at offset 0, the mask, and the rectangle
// it covers, which vid_erase_mask invalidates and vid_mask_initialize places
// the mask at. Nothing reads the last dword.
struct BLTMASK {
  /*0x00*/ TSLink<BLTMASK> link;
  /*0x08*/ HSTRANS hMask;
  /*0x0C*/ int nLeft;
  /*0x10*/ int nTop;
  /*0x14*/ int nRight;
  /*0x18*/ int nBottom;
  /*0x1C*/ DWORD dwUnknown1C;
};
// The mask list, laid out as TSList.h's.
#pragma pack(push, 1)
template <> struct TSList<BLTMASK,TSGetLink<BLTMASK> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<BLTMASK> link;
  // 0x4CB250 (84 B). Unlinks and frees every mask node.
  void Clear(void);
};
#pragma pack(pop)
typedef TSList<BLTMASK, TSGetLink<BLTMASK> > BLTMASKLIST;

OC_X86_LAYOUT(sizeof(BLTMASK) == 0x20, "BLTMASK size        create_mask: push 20h / call SMemAlloc");
static_assert(offsetof(BLTMASK, link) == 0x00, "BLTMASK::link       create_mask: mov dword ptr [esi], 0h / mov dword ptr [esi+4h], 0h");
OC_X86_LAYOUT(offsetof(BLTMASK, hMask) == 0x08, "BLTMASK::hMask      vid_erase_mask: push dword ptr [esi+8h] / call STransDelete");
OC_X86_LAYOUT(offsetof(BLTMASK, nLeft) == 0x0C, "BLTMASK::nLeft      vid_erase_mask: mov ecx, dword ptr [esi+0Ch]");
OC_X86_LAYOUT(offsetof(BLTMASK, nTop) == 0x10, "BLTMASK::nTop       vid_erase_mask: mov edx, dword ptr [esi+10h]");
OC_X86_LAYOUT(offsetof(BLTMASK, nRight) == 0x14, "BLTMASK::nRight     vid_erase_mask: push dword ptr [esi+14h]");
OC_X86_LAYOUT(offsetof(BLTMASK, dwUnknown1C) == 0x1C, "BLTMASK::dwUnknown1C  unread; the slot is spanned by create_mask's push 20h");
OC_X86_LAYOUT(offsetof(BLTMASK, nBottom) == 0x18, "BLTMASK::nBottom    vid_erase_mask: push dword ptr [esi+18h]");

// 0x4CB2B0 create_mask (217 B). `ret 4h`; ECX is the bitmap, EDX and the stack
// word are the top-left it is pinned at. Returns the new BLTMASK.
intptr_t __fastcall create_mask(bmp_t *pBmp, int nArg1, int nArg2);

// 0x4CB390 vid_copy_rect (114 B).
void __fastcall vid_copy_rect(const rect32_t *prc);

// 0x4CB410 vid_draw_mask (75 B).
void vid_draw_mask(void);

// 0x4CB460 vid_erase_mask (150 B). ECX is the BLTMASK to drop, as an integer
// for the reason at the top of this file.
void __fastcall vid_erase_mask(intptr_t nMask);

// 0x4CB500 vid_free (92 B).
void vid_free(void);

// 0x4CB560 vid_lock_surface (37 B). Returns the lock's result.
int __fastcall vid_lock_surface(vidlock_t *pLock);

// 0x4CB590 vid_make_all_invalid (89 B). Returns 1 when the screen is up,
// spelled `int` as dlg_dlgdraw.cpp spells it.
int __fastcall vid_make_all_invalid(void);

// 0x4CB5F0 vid_mask_initialize (200 B). Nullary, though create_mask leaves ECX
// and EDX set before the call.
void vid_mask_initialize(void);

// 0x4CB6C0 vid_paint_invalid (11 B).
void vid_paint_invalid(void);

// 0x4CB6D0 vid_unlock_surface (21 B). `ret 4h`; EDX and the stack word are
// forwarded to the unlock in its own order. Spelled as dlg_effects.cpp spells
// it.
int __fastcall vid_unlock_surface(vidlock_t *pLock, int nFlag, rect32_t *prc);

#endif  // OC117_GDS_VIDBLIT_H
