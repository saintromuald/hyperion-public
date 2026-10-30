// platform.h -- the Win32 type spellings (BYTE, WORD, DWORD, BOOL, ULONG and
// LONG) the game's code is written against, so that every src117 header
// compiles on its own.
//
// On Windows they are <windows.h>'s typedefs with the same underlying types, so
// a TU that sees both gets identical declarations, not a conflict. Keep game
// types and constants out: this header is included from everywhere.
#pragma once

// offsetof, which every layout header's static_asserts use, so that including
// platform.h is all a layout header needs.
#include <stddef.h>

// The pointer-sized integers, for the places where a pointer passes through an
// integer: intptr_t and uintptr_t come with <stddef.h>, and <basetsd.h> is the
// SDK's own home of DWORD_PTR, LONG_PTR, UINT_PTR and ULONG_PTR, which
// <windows.h> includes as well. On x86 each is the 32-bit type it stands in for
// (intptr_t is int, DWORD_PTR is unsigned long), so the 32-bit build compiles to
// the same code (docs/x64-census.md). Off Windows they are stated
// below from <stdint.h>.
#if defined(_WIN32)
#include <basetsd.h>
#else
#include <stdint.h>
typedef uintptr_t DWORD_PTR;
typedef uintptr_t UINT_PTR;
typedef uintptr_t ULONG_PTR;
typedef intptr_t  INT_PTR;
typedef intptr_t  LONG_PTR;
#endif

// A layout check that holds only while pointers are 4 bytes. The engine runs over
// 1.17's image, whose code and data share these structs, so the 32-bit build
// asserts every offset. A 64-bit build lays the structs out its own way and skips
// these; a struct that is also a file, save or network format is listed as open in
// docs/x64-census.md. Spelled as long as static_assert, so a column of checks
// keeps its alignment.
#if defined(_M_IX86)
#define OC_X86_LAYOUT(cond, msg) static_assert(cond, msg)
#else
#define OC_X86_LAYOUT(cond, msg) static_assert(true, msg)
#endif

// An inline function of the game's that other TUs call without seeing its body. MSVC
// emits a copy where the body is, which the others link to; other compilers emit one
// only when asked.
#if defined(_MSC_VER)
#define OC_EMIT
#else
#define OC_EMIT __attribute__((used))
#endif

// A global of 1.17's at its place in the block (or the wide storage), which can be less
// aligned than the x86-64 ABI lets a compiler assume: an array of 16 bytes or more is
// taken to be 16-aligned, so a copy or a clear of it may use aligned SSE stores, which
// fault. MSVC assumes no more than the type's own alignment. tools/build_engine_linux.py
// --check-align lists the globals whose declarations need it.
#if defined(_MSC_VER)
#define OC_UNALIGNED
#else
#define OC_UNALIGNED __attribute__((aligned(1)))
#endif

// DWORD, LONG and ULONG are 32 bits wide, as 1.17's fields are, on every platform
//. Windows keeps `long` at 32 bits, and there they are <windows.h>'s own
// spellings; Linux and Mac make `long` 64 bits wide, so there they are the 32-bit
// types, and the engine's code says LONG and ULONG, never `long`
// (tools/fixed_widths.py).
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef int            BOOL;
#if defined(_WIN32)
typedef unsigned long  DWORD;
typedef unsigned long  ULONG;
typedef long           LONG;
#else
typedef uint32_t       DWORD;
typedef uint32_t       ULONG;
typedef int32_t        LONG;
#endif
static_assert(sizeof(DWORD) == 4 && sizeof(ULONG) == 4 && sizeof(LONG) == 4,
              "DWORD, ULONG and LONG are 1.17's 32-bit types");

// 1.17's C runtime rand and srand, which the game draws creep tiles, sounds and start
// positions from, as the engine's own functions (port/platform/port_standalone.cpp). Named apart
// from the C library's so that no library's call to rand moves the game's numbers, on
// any platform.
extern "C" int __cdecl OcRand(void);
extern "C" void __cdecl OcSrand(unsigned int nSeed);

// Guarded exactly as <windows.h> guards them, so include order does not matter
// and neither definition can win over a different one.
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
