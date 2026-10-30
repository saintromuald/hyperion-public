// compiler.h -- MSVC's keywords and intrinsics for clang and gcc.
//
// The engine's C++ is written in MSVC's dialect: 1.17's calling conventions (no-ops on a
// 64-bit target), __declspec, __forceinline, __int64, and a few of MSVC's intrinsics and
// C runtime names. With MSVC, and with clang-cl, which speaks the same dialect, this file
// is empty. A build with clang or gcc includes it ahead of every file (-include), so no
// file of the engine needs to know which compiler it is under.
//
// Each definition gives what MSVC gives on x64. _rotl and _rotr feed the sync hash and so
// the replay check: they rotate a 32-bit value by the count taken modulo 32, as MSVC's do.
#pragma once

#if !defined(_MSC_VER)

#include <alloca.h>
#include <strings.h>

// Calling conventions: on x64 there is one, and MSVC ignores these.
#define __cdecl
#define __stdcall
#define __fastcall
#define __thiscall
#define __vectorcall
#define __clrcall
#define _cdecl
#define _stdcall
#define _fastcall
#define WINAPI
#define WINAPIV
#define APIENTRY
#define CALLBACK
#define PASCAL

// __declspec(x), for the x the engine uses.
#define __declspec(x) OC_DECLSPEC_##x
#define OC_DECLSPEC_noinline __attribute__((noinline))
#define OC_DECLSPEC_noreturn __attribute__((noreturn))
#define OC_DECLSPEC_dllimport
#define OC_DECLSPEC_dllexport __attribute__((visibility("default")))
#define OC_DECLSPEC_naked __attribute__((naked))
#define OC_DECLSPEC_align(n) __attribute__((aligned(n)))
#define OC_DECLSPEC_selectany __attribute__((weak))
#define OC_DECLSPEC_thread __thread
#define OC_DECLSPEC_novtable
#define OC_DECLSPEC_nothrow __attribute__((nothrow))
#define OC_DECLSPEC_restrict
#define OC_DECLSPEC_noalias
#define __forceinline inline __attribute__((always_inline))
#define __inline inline
#define __restrict__ __restrict

// MSVC's sized integers.
#define __int8 char
#define __int16 short
#define __int32 int
#define __int64 long long

// Intrinsics.
#define _ReturnAddress() __builtin_return_address(0)
#define _AddressOfReturnAddress() __builtin_frame_address(0)
#define __assume(e) ((void)0)
#define __debugbreak() __builtin_trap()
#define _ReadWriteBarrier() __asm__ __volatile__("" ::: "memory")
#define _alloca alloca
// _enable and _disable, 1.17's interrupt markers, do nothing in a program.
#define _enable() ((void)0)
#define _disable() ((void)0)

static inline unsigned int _rotl(unsigned int v, int n)
{
  n &= 31;
  return n ? (v << n) | (v >> (32 - n)) : v;
}
static inline unsigned int _rotr(unsigned int v, int n)
{
  n &= 31;
  return n ? (v >> n) | (v << (32 - n)) : v;
}
static inline unsigned char _rotl8(unsigned char v, unsigned char n)
{
  n &= 7;
  return (unsigned char)(n ? (v << n) | (v >> (8 - n)) : v);
}
static inline unsigned short _rotl16(unsigned short v, unsigned char n)
{
  n &= 15;
  return (unsigned short)(n ? (v << n) | (v >> (16 - n)) : v);
}

// MSVC turns any pointer, a function's or a string literal's, into void * without a cast;
// clang and gcc do not. A few of the game's functions take void * for a file name or a
// callback, and beside each a template overload, compiled off MSVC only, passes its
// argument on through OcVoidPtr.
#ifdef __cplusplus
template <typename T> static inline void *OcVoidPtr(T *p)
{
  return (void *)p;
}
#endif

// fopen with a Windows path: the engine names files as Windows does, with backslashes and
// in any case, and OcFopen (port/platform/port_posix.cpp) finds the file the name means before
// opening it. A function-like macro, so <cstdio>'s `using ::fopen;` is left alone.
#include <stdio.h>
#ifdef __cplusplus
extern "C"
#endif
    FILE *OcFopen(const char *pszName, const char *pszMode);
#define fopen(name, mode) OcFopen((name), (mode))

// The C runtime's names for POSIX's.
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define stricmp strcasecmp
#define strnicmp strncasecmp
// MSVC's _toupper, which moves any character by the distance from 'a' to 'A'. glibc's
// <ctype.h> declares one; Emscripten's and macOS's have only a macro, which Storm.h undefines,
// as the game calls the function.
#if defined(__EMSCRIPTEN__) || defined(__APPLE__)
static inline int _toupper_msvc(int c) { return c - 'a' + 'A'; }
#endif

// h/platform.h's, for a declaration in a file that does not include it.
#ifndef OC_UNALIGNED
#define OC_UNALIGNED __attribute__((aligned(1)))
#endif

#endif // !_MSC_VER
