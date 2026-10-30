// fd_sessn.h -- declarations for src117/lang/fd_sessn.cpp, the download
// session list. Shares TFD_SESSION, TUSER_INFO and TFD_USER with fd.cpp and
// fd_user.cpp; those types live in fd.h.

#ifndef OC117_FD_SESSN_H
#define OC117_FD_SESSN_H

#include "platform.h"
#include "fd.h"

// 0x440950 (71 B). ECX only; unlinks one node with TSList.h's
// complemented-terminator unlink, then frees it under the tag at 0x4EB7C8
// ("delete").
void sub_440950(TUSER_INFO *pNode);

// 0x4409A0 (4 B). ECX a pointer, EDX a second register argument.
void set_fd_session_flags(void *pArg0, int nArg1);

// 0x4409B0 (248 B). ECX the session, EDX 8 bits. Walks the session's inner
// node list for a node whose +8 byte matches, else SMemAllocs a 0x10-byte
// TUSER_INFO, links it at the head and calls sub_4416A0. The SMemAlloc owner
// tag comes through type_info::raw_name on the RTTI object at 0x51076C.
// recv_net_cmd_version_verify passes dword_672960, a TFD_SESSION *.
void sub_4409B0(TFD_SESSION *pSession, BYTE bArg1);

// 0x440AB0 (175 B). ECX only; one consuming caller uses the full EAX.
intptr_t sub_440AB0(intptr_t nArg0);

// 0x440B60 (52 B). ECX only. EAX at the ret is delete_fd_session's leftover,
// not a value this body computes, and no caller reads it.
void sub_440B60(intptr_t nArg0);

// 0x440BA0 (48 B). ECX is a pointer, dereferenced at +0x0C, +0x10 and +0x124;
// returns 0 or 1.
BOOL IsSessionComplete(const TFD_SESSION *pSession);

// 0x440BD0 (6 B). ECX a pointer, EDX a second register argument.
void sub_440BD0(void *pArg0, int nArg1);

// 0x440BE0 (34 B). ECX a pointer, EDX 8 bits.
void sub_440BE0(void *pArg0, BYTE bArg1);

// 0x440D40 (100 B). ECX only.
void sub_440D40(intptr_t nArg0);

#endif  // OC117_FD_SESSN_H
