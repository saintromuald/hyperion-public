// net_record.h -- declarations for src117/lang/net_record.cpp: the network
// turn recorder and its fill and drain paths.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_NET_RECORD_H
#define OC117_NET_RECORD_H

#include "platform.h"

// The recorder these rows reach is the one object at 0x673D40; the members are
// in CNetRecord.h and CNetUnkn.h.

// 0x478430 (74 B). ECX is the recorder; no caller reads EAX.
void sub_478430(void *pArg0);

// 0x478480 (307 B). `ret Ch`: ECX is the recorder, EDX is filled but ignored,
// then the frame's command count, the owners and the scratch object.
int net_record_read_turn(intptr_t nArg0, intptr_t nArg1, void *pArg2, intptr_t nArg3, void *pArg4);

// 0x4785C0 (121 B). ECX is the recorder.
void net_record_grow(intptr_t nArg0);

// 0x478710 (202 B). `ret 4h`: ECX is the scratch object, EDX filled but
// ignored; returns 0 or 1.
BOOL net_record_expand(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2);

// 0x4787E0 (72 B). ECX is the recorder.
void sub_4787E0(intptr_t nArg0);

// 0x478830 (20 B). ECX is the command and EDX its length.
void net_record_cmd(intptr_t nArg0, int nArg1);

// 0x478850 (58 B). Nullary, reached by address through app_add_exit.
void net_record_free();

// 0x478890 (23 B). `ret 4h`: ECX, EDX and one stack word; the one caller reads
// the full EAX.
int sub_478890(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2);

// 0x478980 (98 B). ECX is the file; both callers read the full EAX.
int net_record_write(intptr_t nArg0);

// 0x4789F0 (31 B). Nullary.
void net_record_start_playback();

// 0x478A10 (13 B). Nullary.
void net_record_stop_playback();

// 0x4788B0 (182 B). Nullary and void: the body ends in a tail jump to
// app_add_exit.
void net_record_init();

// 0x478970 (13 B). ECX is a file handle.
int sub_478970(void *pFile);

// Hyperion: a LAN match's record as it is played (port_lanfeed.cpp).
int net_record_live(const BYTE **ppBuf, int *pnOpenAt, DWORD *pdwOpenFrame);
BOOL net_record_append(const void *pData, int nBytes);

#endif  // OC117_NET_RECORD_H
