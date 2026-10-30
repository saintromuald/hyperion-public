// oc_data.h -- where 1.17's data is.
//
// The engine keeps 1.17's .rdata and .data as one block of its own, g_data117, in
// 1.17's layout: the global at 1.17's address va is at g_data117 + (va - 0x4EB000).
// tools/wave117/abs_globals.py defines each global the engine names as an extern at
// its place in the block, and src117/port/platform/port_standalone.cpp fills the block from the
// player's exe as the engine starts. A game global the source spells as a literal
// address in the payload's form goes through OC_VA, which tools/data_block.py writes
// around each such address.
//
// OC_FIXED_DATA keeps the literal address itself, for a test that runs against 1.17's
// own image at 0x400000 (tools/porttest).
#pragma once
#include <stddef.h>

#define OC_DATA_VA 0x4EB000u   // 1.17's .rdata, where the block starts
#define OC_DATA_END 0x6DF000u  // the end of 1.17's .data pages

#ifdef OC_FIXED_DATA
#define OC_VA(va) ((size_t)(va))
#else
extern "C" unsigned char g_data117[];
#define OC_VA(va) (reinterpret_cast<size_t>(g_data117) + ((size_t)(va) - OC_DATA_VA))
#endif
