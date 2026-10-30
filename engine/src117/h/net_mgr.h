// net_mgr.h -- declarations for src117/lang/net_mgr.cpp, the network turn manager:
// queueing commands, sending and receiving turns, latency and sync.
//
// packet_briefing_start, sub_475D10 and sub_476B20 are declared in gluRdy.h and
// netmgr_queue_cmd in net_cmds.h, which net_mgr.cpp includes.

#ifndef OC117_NET_MGR_H
#define OC117_NET_MGR_H

#include "platform.h"

// 0x4765A0 (67 B) -- nullary void: no register input, no stack parameters, and
// both callers (leave_game, net_exit) discard EAX. The body uses x87/SSE, so a
// float return would not show in EAX, but neither caller reads one.
void net_init_sync();

#include "net_cmds.h"   // net_cmd_byte_t, netmgr_queue_cmd, net_send_turn

// 0x475AC0 (85 B) -- nullary. Both exits materialise a 0 or 1 in EAX (`mov eax,1`
// on one, `sbb eax,eax / neg eax` on the other).
int GetTurnPackets();

// 0x475B20 (17 B) -- ECX and EDX are forwarded untouched to sub_475B40, which
// reads both as 32 bits; `ret 4h` adds one stack word. EAX at the `ret` is
// sub_475B40's.
int sub_475B20(int nArg0, intptr_t nArg1, int nArg2);

// 0x475B40 (281 B) -- ECX and EDX both read as 32 bits, `ret 8h`: two stack
// words.
int sub_475B40(int nArg0, intptr_t nArg1, int nArg2, int nArg3);

// 0x475C60 (17 B) -- the same forwarding shape as sub_475B20.
int sub_475C60(int nArg0, intptr_t nArg1, int nArg2);

// 0x475C80 (55 B) -- nullary; every `ret` materialises an explicit 0 or 1, and
// sub_476B20 stores the result through its out-parameter.
int sub_475C80();

// 0x476500 (154 B) -- nullary, void. ProgressNetwork calls it.
void SendTurns();

// 0x4765F0 (6 B) -- `mov eax, [0x672990] / ret`, the whole body, so it returns
// int though its one caller, net_dlg_radio_init, discards EAX.
int net_get_latency();

// 0x476600 (73 B) -- nullary. Ends in a tail `jmp sub_470910`, which
// net_chat_cmds.h declares void.
void net_mgr_init();

// 0x476650 (437 B) / 0x4768A0 (258 B) -- nullary, void; ProgressNetwork calls
// both.
void net_mgr_process_lobby_turns();
void net_mgr_process_turns();

// 0x476B80 (117 B) -- nullary; EAX at the `ret` is inherited from a callee and
// no caller consumes it -> void.
void netmgr_process_messages();

// 0x476C00 (122 B) -- nullary; every `ret` materialises 0 or 1 and all three
// consuming callers use the full EAX without narrowing -> BOOL.
BOOL ProgressNetwork();

// 0x476D10 (103 B) -- one register argument, dereferenced: `movzx ecx, byte ptr
// [ecx+1h]` is the one-byte field of a net_cmd_byte_t. EAX at the `ret` is
// map_msg_chat_msg's leftover.
void net_recv_set_latency(const net_cmd_byte_t *pCmd);

// 0x476D80 (21 B) -- two global stores and a bare `ret`; EAX is never written.
void netmgr_replay_init();

// 0x476DA0 (62 B) -- nullary; the only exits are a bare `ret` and a conditional
// tail `jnz packet_sync`, and packet_sync is void (src117/h/net_sync.h).
void netmgr_send_turn();

// 0x476DE0 (28 B) -- the parameter is 32-bit though the body reads only CL: the
// one caller, net_dlg_cmd, passes it with `mov ecx, esi`, and declared BYTE it
// would emit `mov cl, bl` and assign its registers differently.
void netmgr_set_latency(int nLatency);

// 0x476E00 (21 B) -- ECX read as 32 bits.  Both call sites (run_game) set ECX
// and discard EAX; the body's own exits are `jmp packet_sync` and a bare `ret`,
// and packet_sync is void.
void netmgr_set_sync(int nEnabled);


// 0x475D30 (1,585 B). The game turn's command dispatcher; net_mgr_process_turns
// calls it.
void net_process_game_turn(char *pData, unsigned int nSize, int nFlag);

#endif  // OC117_NET_MGR_H
