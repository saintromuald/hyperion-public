// cur.h -- declarations for src117/lang/cur.cpp: the cursor's graphic and its
// animation, drawn as image layer 0.
//
// Class members belong in the class header; this file owns the free functions,
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_CUR_H
#define OC117_CUR_H

#include "platform.h"

// Five of cur.cpp's functions are declared elsewhere and not repeated here, since
// a second spelling would conflict: cur_animate and cur_load in glues.h,
// cur_update in event_Win.h, cur_hide and cur_show in TrigAct.h (with an explicit
// __fastcall, which mangles the same). A definition in cur.cpp matches them.

// cur_deinit -- 0x43E450, 18 B. Nullary, void.
void cur_deinit();

// cur_display_init -- 0x43E470, 29 B. Nullary, void.
void cur_display_init();

// cur_display_update -- 0x43E490, 203 B. `ret 8h`: ECX and EDX are read 16 bits
// wide, so WORD, and the two stack dwords are never read. Void.
// The engine types the two stack slots as img.h's PFN_IMG_UPDATE, the four parameters img_update_real passes:
// _img_list[0]'s callback.
struct bmp_t;
struct bmpsrc_t;
void __fastcall cur_display_update(WORD wArg0, WORD wArg1, bmp_t *nArg2, bmpsrc_t *nArg3);

// cur_free -- 0x43E560, 35 B. One 32-bit argument in ECX; void.
void cur_free(intptr_t nArg0);

// cursor_set_image -- 0x43E5E0, 20 B. One 32-bit argument in ECX: cursor.cpp's
// cursor_set tail-jumps here with a slot of the cursor pointer table. Void.
void cursor_set_image(intptr_t nArg0);

// draw_cursor -- 0x43E690, 61 B. `ret 4h`: two 32-bit register arguments and one
// stack dword. Void.
void draw_cursor(intptr_t nArg0, int nArg1, int nArg2);

// Callees cur.cpp reaches that no other header declares.

// img_cursor_invalid -- 0x4CADB0, 58 B (oracle TU gds_local.cpp). Nullary, void.
void img_cursor_invalid();

// ReadGroupFile (0x444A60) is not declared here: cur.cpp repeats the spelling
// glues.cpp and opt_dlgs.cpp give it, and a declaration with another signature
// would mangle differently.

#endif  // OC117_CUR_H
