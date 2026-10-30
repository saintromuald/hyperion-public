// point16.h -- a signed 16-bit point, x then y, four bytes: the dialog code's
// control points and the first half of the order queue's order_target_t. Signed
// because pt_in_rect compares it with signed branches, and a point relative to a
// dialog's origin goes negative above and left of it. This is the one
// definition; dialog.h includes it.
//
// Include after <stddef.h> (the static_asserts use offsetof).
#pragma once

struct point16_t {
  /*0x00*/ short x;
  /*0x02*/ short y;
};

// The size matters to both users: the dialog bodies pass its address, and the
// order bodies pass it by value inside an 8-byte order_target_t, two dwords.
static_assert(sizeof(point16_t)      == 0x04, "point16_t size  ([ebp-4] .. [ebp-1]; and the low half of order_target_t)");
static_assert(offsetof(point16_t, x) == 0x00, "point16_t::x    global_evt_2_local_point: mov word ptr [ecx], ax");
static_assert(offsetof(point16_t, y) == 0x02, "point16_t::y    global_evt_2_local_point: mov word ptr [ecx+2h], ax");
