// net_rand.h -- declarations for src117/lang/net_rand.cpp, the networked PRNG:
// the entry points other TUs call. Free functions are __fastcall through /Gr and
// are declared without the keyword.

#ifndef OC117_NET_RAND_H
#define OC117_NET_RAND_H

#include "platform.h"

// 0x4781F0 (7 B). ECX only; EAX is never set. recv_net_cmd_seed_init hands it
// the dword unpacked from the command payload.
void net_set_fixed_seed(int nSeed);

// 0x4781D0 (6 B). Nullary; both callers, sub_47A250 and write_player_data, read
// the full EAX.
int net_randsequence();

// 0x4781C0 (12 B). ECX only. Returns the previous flag value, so a caller can
// restore it; three callers read the full EAX.
int set_rand_enabled(int nEnabled);

// 0x4781E0 (6 B). Nullary; returns the seed net_set_fixed_seed stores.
int net_get_fixed_seed();

#endif  // OC117_NET_RAND_H
