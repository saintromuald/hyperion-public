// fd_user.h -- the download layer's per-user records and transfer state
// machine, oracle 0x440DB0-0x44197F. on_fd_transfer in fd.cpp jump-tables into
// seven of these on the byte at [state+0]; each is `ret 4h` with ECX and EDX
// forwarded, so all seven share one shape. TFD_USER and TFD_SESSION live in
// fd.h.

#ifndef OC117_FD_USER_H
#define OC117_FD_USER_H

#include "platform.h"
#include "fd.h"
#include "gds_log.h"

// The seven on_fd_transfer arms, in jump-table index order 0..6: sub_440DB0,
// sub_441150, sub_4415E0, sub_441660, sub_4413B0, sub_441520, sub_4415A0.

// 0x440DB0 (117 B). No caller at all; the explicit immediates on every path make
// its return a BOOL.
BOOL sub_440DB0(intptr_t nArg0, intptr_t nArg1, BYTE bArg2);

// 0x440E30 (61 B). ECX is dereferenced. sub_440F40 and four later bodies need
// this one out of line, so its definition carries __declspec(noinline) and has
// to stay above its callers in the TU.
void sub_440E30(void *pArg0);

// 0x440E70 (103 B). ECX is forwarded untouched to a callee, so comp_crc32's
// declaration is what sets its width.
int sub_440E70(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2);

// 0x440EE0 (89 B). ECX dereferenced; every ret materialises 0 or 1.
BOOL sub_440EE0(void *pArg0);

// 0x440F40 (100 B). ECX read as 8 bits.
void sub_440F40(BYTE bArg0);

// 0x440FB0 (101 B). Called only through a pointer. It reads no register input and
// pops 8 bytes, so it is __stdcall with two stack arguments.
BOOL __stdcall sub_440FB0(intptr_t nArg0, void *pArg1);

// 0x441020 (84 B). Two consuming callers use the full EAX; every ret is 0 or 1.
BOOL fd_has_map_extension(intptr_t nArg0);

// 0x441080 (196 B). ECX dereferenced, EDX 32 bits, one stack word read three
// times.
BOOL sub_441080(void *pArg0, intptr_t nArg1, int nArg2);

// 0x441150 (605 B). EDX is dereferenced. Its one consumer is on_fd_transfer's tail
// jump and the body ends `mov eax, 1h / ret 4h`, so it returns BOOL. arg0's slot
// is read both 1 and 4 bytes wide; it is declared int.
BOOL sub_441150(intptr_t nArg0, void *pArg1, int nArg2);

// 0x4413B0 (361 B). arg0's slot is read both narrow and wide, as in sub_441150.
BOOL sub_4413B0(intptr_t nArg0, intptr_t nArg1, int nArg2);

// 0x441520 (121 B).
BOOL sub_441520(intptr_t nArg0, intptr_t nArg1, int nArg2);

// 0x4415A0 (60 B). ECX dereferenced.
BOOL sub_4415A0(void *pArg0, intptr_t nArg1, int nArg2);

// 0x4415E0 (123 B). Returns 1: the body ends `lea eax, [edi+1h] / ret 4h` with EDI
// decremented to zero.
BOOL sub_4415E0(intptr_t nArg0, intptr_t nArg1, BYTE bArg2);

// 0x441660 (61 B). ECX dereferenced.
BOOL sub_441660(void *pArg0, intptr_t nArg1, BYTE bArg2);

// 0x4416A0 (125 B). ECX is a BYTE: its callers load it with `mov cl`, and the
// `push` of it inside is gpfnFdSend's one-byte third argument, not a dword
// read. Same for sub_441850 and sub_441890.
void sub_4416A0(BYTE bArg0, intptr_t nArg1);

// 0x441720 (185 B). Two consuming callers use the full EAX.
int sub_441720(intptr_t nArg0, int nArg1);

// 0x4417E0 (57 B). ECX dereferenced.
void sub_4417E0(void *pArg0);

// 0x441820 (47 B). EDX dereferenced. ECX is a BYTE because sub_4413B0 loads it
// with `mov cl`; this body only forwards it to gpfnFdSend's one-byte third argument.
void sub_441820(BYTE bArg0, void *pArg1);

// 0x441850 (55 B). EDX 8 bits; the stack word is loaded and dereferenced.
void sub_441850(BYTE bArg0, BYTE bArg1, void *pArg2);

// 0x441890 (33 B). EDX 8 bits.
void sub_441890(BYTE bArg0, BYTE bArg1);

// 0x4418C0 (189 B).
int sub_4418C0(intptr_t nArg0);

// Callees from other TUs that no header of ours declares.

// 0x413AB0 comp_crc32, compress.cpp (37 B). ECX and EDX both 32 bits; three
// consuming callers use the full EAX.
int comp_crc32(intptr_t nArg0, int nArg1);

// 0x46B8D0 set_map_filename, map_load.cpp (58 B). ECX and EDX both 32 bits.
void set_map_filename(intptr_t nArg0, int nArg1);

// 0x4CC370 pfclose, gds_file.cpp (10 B). Four consuming callers use the full
// EAX.
int __fastcall pfclose(void *pFile);

// 0x4CC3C0 pfopen, gds_file.cpp (108 B). EDX is read with movsx as 16 bits, so
// the mode argument is signed.
void *__fastcall pfopen(const char *pszName, short sMode);

#endif  // OC117_FD_USER_H
