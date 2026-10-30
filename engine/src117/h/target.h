// target.h -- declarations for src117/lang/target.cpp, oracle 0x4B2830..0x4B2936:
// the targeting cursor mode, turned on and off around a pending order and
// dispatching the order the player finally picks. Free functions are __fastcall
// through /Gr and are declared without the keyword. target_init is declared in
// game.h and target_is_on in statbtn.h.

#ifndef OC117_TARGET_H
#define OC117_TARGET_H

#include "platform.h"
#include "typelimits.h"  // Hyperion: order_id_t

// 0x4B2850. Dispatches the order chosen in targeting mode. ECX is read as 32
// bits, EDX as 16. The one consuming caller uses only AL, so a BYTE. wCard is
// compared against 0E4h, the card id this TU also hands to statcmd_set_ctrl.
order_id_t target_order(int nApply, WORD wCard);

// 0x4B28B0. Puts the cursor into target-selection mode, storing its three
// arguments as bytes into the state block at 0x6BE651..0x6BE653. The first two
// are BYTE: the call sites load them with `mov cl, 8`, not `mov ecx, 8`. The
// third is unsigned: order_heal and order_patrol pass 0xB1 and 0x98. The name
// bOrderAlt is a guess.
void target_on(order_id_t bCursor, order_id_t bOrder, order_id_t bOrderAlt);

// 0x4B2900. Leaves target-selection mode: clears the enable byte at 0x6BE650,
// blanks the targeting message, restores the event handlers, then tail-jumps
// into statcmd_set_ctrl with card 0E4h. Nullary: it opens by zeroing ECX.
void target_off();

#endif  // OC117_TARGET_H
