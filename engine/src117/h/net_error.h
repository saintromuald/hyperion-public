// net_error.h -- declarations for src117/lang/net_error.cpp, the deferred
// network-error report. No default arguments: every caller passes every
// argument, as 1.17's call sites do.

#ifndef OC117_NET_ERROR_H
#define OC117_NET_ERROR_H

#include "platform.h"

// 0x4748D0 (169 B). `ret 8h`: ECX, EDX and two stack words. ECX is the string
// id, latched first, and EDX the text. No caller reads EAX.
void sub_4748D0(int nArg0, const char *pszText, intptr_t nArg2, int nArg3);

// 0x474A00 (104 B). `ret 0Ch`: ECX, EDX and three stack words. ECX is a string
// id; the rest are the statement, the file, the line and the leave flag.
void err_popup(int nStrId, intptr_t nArg1, intptr_t nArg2, int nArg3, int nArg4);

// 0x474B80 (105 B). `ret 8h`: ECX, EDX and two stack words, a string id and the
// file, line and leave flag; Storm's last error becomes err_popup's statement.
void net_error_popup(int nStrId, const char *pszFile, int nLine, int nLeave);

// 0x474A70 (253 B), `ret 8h`. Every caller passes (id, text, 0, 0) or
// actualize's latched pair.
void __fastcall net_error_show_error(int nStringId, const char *pszText,
                                     intptr_t nArg2, int nArg3);

#endif  // OC117_NET_ERROR_H
