// target.cpp -- the targeting cursor mode: turning it on and off and
// dispatching the chosen order.
//
// Bodies are in ascending oracle address, 1.17's own order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/target.h"
#include "../h/statbtn.h"       // targeting_msg, target_is_on
#include "../h/statcmd.h"       // statcmd_set_ctrl
#include "../h/event_Win.h"     // game_reset_handlers
#include "../h/game.h"          // target_init
#include "../h/net_cmds.h"      // error_get_str
#include "../gen/globals117.h"

// The globals this TU reaches, each spelled once for the standalone build and
// once for the payload.

// The targeting state block, four consecutive bytes at 0x6BE650, bounded above
// by allowed_errcode_g at 0x6BE654.
#ifdef OC_DLL_MODE
  #ifndef sgbTarg
  #define sgbTarg (*reinterpret_cast<BYTE *>(OC_VA(0x6BE650u)))
  #endif
#else
extern BYTE sgbTarg;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbOrder
  #define sgbOrder (*reinterpret_cast<BYTE *>(OC_VA(0x6BE651u)))
  #endif
#else
extern order_id_t sgbOrder;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbOrder2
  #define sgbOrder2 (*reinterpret_cast<BYTE *>(OC_VA(0x6BE652u)))
  #endif
#else
extern order_id_t sgbOrder2;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgbOrder3
  #define sgbOrder3 (*reinterpret_cast<BYTE *>(OC_VA(0x6BE653u)))
  #endif
#else
extern order_id_t sgbOrder3;
#endif

// 0x667288, gMapCursor, which game_evt.cpp owns. Every access here is a WORD
// store.
#ifdef OC_DLL_MODE
  #ifndef gMapCursor
  #define gMapCursor (*reinterpret_cast<WORD *>(OC_VA(0x667288u)))
  #endif
#else
extern WORD gMapCursor;
#endif

// Clears the targeting enable byte. Nullary.
void target_init() {
  sgbTarg = 0;
}

// Reads the same byte back. The return is produced in AL and nothing widens it.
BYTE __fastcall target_is_on() {
  return sgbTarg;
}

// Leaves targeting mode and returns the order byte the player settled on. Both
// ternary arms are plain global reads, so both loads hoist above the compare and
// the select is a cmov. The ternary's != has to name sgbOrder3 first: the false
// arm is the one loaded into EAX, and the equal-first spelling swaps the two
// globals and flips the cmov to cmovz.
order_id_t target_order(int nApply, WORD wCard) {
  sgbTarg = 0;
  targeting_msg(NULL);
  game_reset_handlers();
  gMapCursor = 0;
  statcmd_set_ctrl(0xE4);
  if (nApply)
    return sgbOrder;
  return wCard != 0xE4 ? sgbOrder3 : sgbOrder2;
}

// Enters targeting mode: the three arguments fill the state block, the prompt
// comes from error string 3Fh, the card is 0E5h, and statcmd_set_ctrl is a call
// rather than a tail jmp because of the ret 4h. The sgbOrder store has to precede
// the sgbTarg store; it is the one ordering of the three stores that schedules as
// the oracle does.
void target_on(order_id_t bCursor, order_id_t bOrder, order_id_t bOrderAlt) {
  sgbOrder = bCursor;
  sgbTarg = 1;
  sgbOrder2 = bOrder;
  sgbOrder3 = bOrderAlt;
  targeting_msg(error_get_str(0x3F));
  game_reset_handlers();
  gMapCursor = 2;
  statcmd_set_ctrl(0xE5);
}

// Leaves targeting mode. statcmd_set_ctrl is reached by a tail jmp, so its call
// has to stay the last statement. The `xor ecx, ecx` at entry is targeting_msg's
// argument, not an incoming one.
void target_off() {
  sgbTarg = 0;
  targeting_msg(NULL);
  game_reset_handlers();
  gMapCursor = 0;
  statcmd_set_ctrl(0xE4);
}
