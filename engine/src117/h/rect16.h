// rect16.h -- rect16_t, the 16-bit map rectangle that get_units_in_rect
// (0x422330) and the unit-finder family take. TUs across the game pass one, so
// it has this one definition and one set of static_asserts: a copy with left
// and top transposed would still compile and select the wrong units.
//
// Include it after <stddef.h> (the static_asserts use offsetof). It includes
// no project header.
#pragma once

// Built on the stack by order_select_larva (0x47C670) and read by
// get_units_in_rect. The order is left, top, right, bottom, not left, right,
// top, bottom. The members are signed: get_units_in_rect loads them with movsx,
// and a rectangle clipped at the map edge has a negative left or top.
struct rect16_t {
  /*0x00*/ short left;
  /*0x02*/ short top;
  /*0x04*/ short right;
  /*0x06*/ short bottom;
};

static_assert(sizeof(rect16_t)           == 0x08, "rect16_t size    ([ebp-8] .. [ebp-1])");
static_assert(offsetof(rect16_t, left)   == 0x00, "rect16_t::left     [ebp-8]");
static_assert(offsetof(rect16_t, top)    == 0x02, "rect16_t::top      [ebp-6]");
static_assert(offsetof(rect16_t, right)  == 0x04, "rect16_t::right    [ebp-4]");
static_assert(offsetof(rect16_t, bottom) == 0x06, "rect16_t::bottom   [ebp-2]");
