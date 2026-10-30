// order_target.h -- the 8-byte (where, what) pair the order family calls a target,
// passed by value. It is the second parameter of every order-installing entry
// point in src117/lang/CUnitOrder.cpp, and what the `_Target` in their names
// means.
//
// The two halves are one struct and not two parameters: CUnit::SetOrder_Target
// forwards the middle two of its three stack slots as a unit under a `ret 0Ch`,
// and the 8-byte frame the rest of the family reserves is this struct's size.
//
// The basename is not target.h on purpose: the quarantined reference tree has a
// target.h, and a header of ours that shares a basename with one of theirs can be
// resolved to the wrong file (src117/h/CTarget.h).
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "point16.h"   // the signed 16-bit point that is this pair's first half
#include "pos.h"       // Hyperion: mpoint_t, that point of 4-byte positions on x64

class CUnit;           // pointer only -- see src117/h/CTarget.h on why not an include

// order_target_t -- where an order points, and what it points at. The position is
// the low dword and the targeted unit the high one, the order MSVC pushes a
// by-value struct in; CUnit::PrependOrder_CUnit types the second half, storing
// its own CUnit * argument there and dereferencing the same value for the sprite
// it reads the position from. The size has to be 8: MSVC then pushes two dwords
// and reserves the 8-byte frame the oracle reserves, and at any other size every
// body in the family loses the call.
struct order_target_t {
  /*0x00*/ mpoint_t   pt;   // Hyperion: point16_t in 1.17 (h/pos.h)
  /*0x04*/ CUnit     *pUnit;
};

// Compile-time only: these emit no code.
OC_X86_LAYOUT(sizeof(order_target_t)          == 8,    "order_target_t size -- pushed as TWO dwords; the 8-byte frame IS this");
static_assert(offsetof(order_target_t, pt)    == 0x00, "order_target_t::pt    -- the LOW dword, pushed second");
OC_X86_LAYOUT(offsetof(order_target_t, pUnit) == 0x04, "order_target_t::pUnit -- the HIGH dword, pushed first");
