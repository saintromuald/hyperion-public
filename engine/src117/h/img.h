// img.h -- img_t, one entry of the overlay-image table `_img_list`, and the two free
// functions that maintain it.
//
// The name `_img_list` is Blizzard's, from sc-refs/sheets/data_naming_1.17.csv. The struct
// is in a header and not in a .cpp because the table has several owners: dlg_dlgdraw.cpp
// reads element 0 (the cursor image) and element 1 (the context image), and the
// gds/image.cpp layer that maintains the array is still unported.
//
// Its scalars are spelled `unsigned char` and `short` rather than Win32 aliases, because
// src117/lang/dlg_dlgdraw.cpp includes this header before any alias exists.

// Hyperion: the header now carries the whole element, the two pointers
// and the callback type included, and the one declaration of _img_list, whose
// layout changes at 64 bits and which every TU has to see the same. CtxtHelp.cpp's and dlg_dlg.cpp's imglayer_t,
// placebox.cpp's placebox_img_t and gds_image.cpp's imgdraw_t were views of it;
// the interior names byte_6D0BCC, byte_6D0BE0, byte_6D0BF4, byte_6D0BF5,
// dword_6D0C00 and dword_6D0C04 are its fields.
#pragma once
#include "platform.h"
#include "oc_data.h"

struct bmp_t;
struct bmpsrc_t;

// One entry of the overlay-image table `_img_list` -- the small software-drawn images the
// dialog layer composites over the game surface. Element 0 is the mouse cursor, element 1
// the "context" image. The stride, 0x14, is what makes this an array element rather than
// two globals: UpdateCursorRect reads the record at 0x6D0B90 and img_update an identical
// one at 0x6D0BA4, and the 0xA0 span to the next named global is exactly 8 of them.

// The four coordinates are read `movsx`, so they are signed, and both readers build
// `SetRect(&rc, x, y, x + w - 1, y + h - 1)`, which makes +0x06 and +0x08 an extent rather
// than a second corner. +0x01 is the layer's flags: img_update_real forces a draw on bits 0
// and 5, records in bit 2 that the dirty map asked for one, reads bit 1 as a pending draw
// and clears the low three bits after drawing; dlg_mgr_init sets 0x20 on layer 2, and
// gamemap_update reads bit 0 of layer 5 as its full-repaint flag.

// The callback takes the layer's bitmap and a bmpsrc_t, with ECX and EDX zeroed at
// img_update_real's call. 1.17 spells three of its five __stdcall over the two pushed
// words, which pops the same 8 bytes on x86; at 64 bits there is one convention and the
// bitmap would land in the first parameter, so every callback takes the four parameters.
typedef void(__fastcall *PFN_IMG_UPDATE)(unsigned short nX, unsigned short nY,
                                         bmp_t *pBmp, bmpsrc_t *pSrc);

struct img_t {
  /*0x00*/ unsigned char bInUse;
  /*0x01*/ unsigned char bFlags;
  /*0x02*/ short nX;
  /*0x04*/ short nY;
  /*0x06*/ short nWidth;
  /*0x08*/ short nHeight;
  /*0x0A*/ unsigned char _unmapped0A[2];
  /*0x0C*/ bmp_t *pBmp;
  /*0x10*/ PFN_IMG_UPDATE pfnUpdate;
};

OC_X86_LAYOUT(sizeof(img_t)              == 0x14, "img_t size       stride 0x6D0BA4 - 0x6D0B90, and 0xA0 span / 0x14 == 8 elements");
static_assert(offsetof(img_t, bInUse)    == 0x00, "img_t::bInUse    UpdateCursorRect: cmp byte ptr [0x6D0B90], 0h");
static_assert(offsetof(img_t, bFlags)    == 0x01, "img_t::bFlags    img_invalid: or byte ptr [esi*4+6D0B91h], 1h");
static_assert(offsetof(img_t, nX)        == 0x02, "img_t::nX        UpdateCursorRect: movsx edx, word ptr [0x6D0B92]");
static_assert(offsetof(img_t, nY)        == 0x04, "img_t::nY        UpdateCursorRect: movsx ecx, word ptr [0x6D0B94]");
static_assert(offsetof(img_t, nWidth)    == 0x06, "img_t::nWidth    UpdateCursorRect: movsx eax, word ptr [0x6D0B96]");
static_assert(offsetof(img_t, nHeight)   == 0x08, "img_t::nHeight   UpdateCursorRect: movsx eax, word ptr [0x6D0B98]");
OC_X86_LAYOUT(offsetof(img_t, pBmp)      == 0x0C, "img_t::pBmp      img_update_real: push dword ptr [esi+8h], esi biased by 4");
OC_X86_LAYOUT(offsetof(img_t, pfnUpdate) == 0x10, "img_t::pfnUpdate img_update_real: mov eax, dword ptr [esi+0Ch] / call eax");

// _img_list -- img_t[8] @0x6D0B90 (gds/image.cpp). Element 0 is the cursor, 1 the
// context-help bubble, 2 the dialogs, 3 and 4 the placement boxes and 5 the map. The
// dll-mode arm is a reference to the array, so sizeof and &_img_list[i] mean the same in
// both forms.
#ifndef OC__IMG_LIST
#define OC__IMG_LIST
#ifdef OC_DLL_MODE
#define _img_list (*reinterpret_cast<img_t (*)[8]>(OC_VA(0x6D0B90u)))
#else
extern img_t _img_list[8];
#endif
#endif

// The interior names 1.17's code reaches layers 3 to 5 by, as the fields they are.
#define byte_6D0BCC  (_img_list[3].bInUse)
#define byte_6D0BE0  (_img_list[4].bInUse)
#define byte_6D0BF4  (_img_list[5].bInUse)
#define byte_6D0BF5  (_img_list[5].bFlags)

// The two free functions that maintain this table, both defined in the still-unported
// gds/image.cpp layer: these are declarations for the generated thunks, not a claim on the
// bodies. They are declared here so that generated code reaching them does not resolve the
// declaration in the quarantined cpp-1.17 tree; src117/h is include root 0, so this one
// out-ranks that one rather than arguing with it.

// Both are __fastcall with a BYTE layer index in ECX and no stack parameters. The return is
// `int` rather than `void`: a value really is produced, through img_force's tail call into
// img_area_invalid, and all 16 call sites discard it. A discarded result compiles the same
// either way, and the spelling only has to be consistent, since C++ mangles the return
// type into the thunk's symbol.
int img_invalid(unsigned char nLayer);
int img_force(unsigned char nLayer);
