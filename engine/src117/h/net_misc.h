// net_misc.h -- declarations for src117/lang/net_misc.cpp: Storm's player join and
// leave callbacks, dropping a player, the game results and the Battle.net check.
// The file name is the one 1.17's own net_error_popup string at 0x4EF2C4 gives.
//
// get_valid_str_len, sub_4771E0, IsGameHost and sub_477E00 are declared in
// net_cmds.h, get_num_active_players in minimap.h and sub_476F60 in opt_dlgs.h,
// which net_misc.cpp includes. This header includes none of them: the generated
// thunk table includes it, and their <ctype.h> would turn `_toupper` into a macro.

#ifndef OC117_NET_MISC_H
#define OC117_NET_MISC_H

#include "platform.h"

// 0x477860 (119 B) -- nullary; EAX at the `ret` is call:net_error_popup's
// leftover and no caller consumes it -> void. recv_net_cmd_make_public calls it
// on the arm where the sender is not the local player.
void sub_477860();

// 0x4777E0 (30 B) -- nullary; both exits materialise an explicit 0 or 1 and every
// caller tests EAX. It is declared only here, the header of the TU that defines
// it; opt_dlgs.h and gametype.cpp include this header for it.
int IsBNetGame();

// 0x476EF0 (104 B) -- one register argument, read only as a truth value, so int.
// Reads the game mode through SNetGetGameInfo, rewrites one bit, writes it back
// with SNetSetGameMode. EAX at the `ret` is net_error_popup's leftover.
void sub_476EF0(int nEnable);

// 0x477060 (47 B) -- ECX is the player id (compared against 8, then pushed to
// SNetDropPlayer), EDX survives into wrap_map_msg, and `ret 4h` adds the flags
// word SNetDropPlayer receives.  EAX is never materialised here -> void.
void net_drop_player(int nPlayerId, intptr_t nArg1, DWORD dwFlags);

// 0x4775F0 (19 B) -- nullary; clears nine bytes of results state with one SSE
// store and a byte store.  EAX is never written -> void.
void clear_game_results();

// 0x477850 (13 B) -- nullary; `xor eax,eax / cmp byte / setz al`, so the callee
// materialises its own 0/1 -> BOOL.
BOOL IsLoss();

// 0x477A00 (49 B) -- Storm's player-leave callback: __stdcall with one stack
// argument, the event record, and no register input. Its address is registered,
// never called directly, so the convention must be spelled.
void __stdcall snet_playerleave_cb(void *pEvent);

// 0x477A40 (20 B) -- nullary; two 16-byte SSE stores zeroing the 32-byte player
// table at 0x6737F4.  EAX is never written -> void.
void net_misc_init();

// 0x477A60 (437 B) / 0x477C20 (380 B) -- both take ECX and EDX as 32 bits;
// sub_477A60 adds one stack word. sub_477DA0 and sub_477E00 call them.
void sub_477A60(int nArg0, int nArg1, int nArg2);
void sub_477C20(int nArg0, int nArg1);

// 0x477DA0 (87 B) -- nullary; walks the 8-slot player table and returns nothing.
void sub_477DA0();

// 0x4778E0 (283 B). Storm event 2, registered rather than called, so __stdcall,
// and the one stack word is the event record. Defined in net_misc.cpp, though its
// row names net_glue.cpp.
void __stdcall snet_playerjoin_cb(void *pEvent);

// 0x477E20 (11 B). Nullary; clears the results-written flag.
void clear_results_written();

// 0x477090 (323 B). Nullary; returns 0 only when the host's map session could
// not be opened, and its one caller tests EAX.
int sub_477090();

// 0x477270 (867 B). ECX is the eight results and EDX the optional local one.
// Spelled as gluScore.cpp's local declaration spells it.
void __fastcall sub_477270(int *pnStatus, int *pnLocal);

#endif  // OC117_NET_MISC_H
