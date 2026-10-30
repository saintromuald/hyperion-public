// Exception.h -- declarations for src117/lang/Exception.cpp, the unhandled-
// exception filter and the status-code name table behind it. Free functions
// are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_EXCEPTION_H
#define OC117_EXCEPTION_H

#include "platform.h"
#include "Storm.h"   // EXCEPTION_POINTERS, LONG

// 0x4025D0 (742 B). Writes the name of a Win32 status code into the caller's
// buffer. ECX is the code, EDX the buffer and the size arrives on the stack.
void GetExceptionNameWin32(DWORD dwCode, char *pszName, size_t cbName);

// 0x402A10 (147 B). The filter SetExceptionHandler installs. ECX and EDX are
// both live in and the EXCEPTION_POINTERS arrives on the stack; the return is
// the LONG an unhandled-exception filter is defined to give back.
LONG CrashyExceptionFilter(size_t cbName, void *pvUnused, EXCEPTION_POINTERS *pep);

#endif  // OC117_EXCEPTION_H
