// point32.h -- a 32-bit point passed by value in two dword stack slots, x then
// y, as CUnit::sub_4BC460 (0x4BC460) takes one. Two int parameters give the same
// slots but home the two values in the other callee-saved registers.
//
// src117/lang/minimap.cpp spells the same layout locally as point32_t and does
// not include this header.
//
// Include after <stddef.h> (the static_asserts use offsetof).
#pragma once
#include "platform.h"  // LONG and ULONG

struct point32_t {
  /*0x00*/ LONG x;
  /*0x04*/ LONG y;
};

static_assert(sizeof(point32_t) == 0x08, "point32_t size   two dword stack slots");
static_assert(offsetof(point32_t, x) == 0x00, "point32_t::x   CUnit::sub_4BC460: mov edi, dword ptr [ebp+8h]");
static_assert(offsetof(point32_t, y) == 0x04, "point32_t::y   CUnit::sub_4BC460: mov ebx, dword ptr [ebp+0Ch]");
