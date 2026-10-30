// ErrorHandler.h -- the crash reporter's handlers in src117/lang/gds_appcrit.cpp:
// the exception handler crashy calls and the report header both it and
// BlizzErrFunc write.
//
// The members are static: neither body has a `this`, and ECX is the first
// argument, /Gr's __fastcall for a static member.

#ifndef OC117_ERRORHANDLER_H
#define OC117_ERRORHANDLER_H

#include "platform.h"
#include "Storm.h"    // <windows.h>, which crashy.h assumes
#include "crashy.h"

struct ErrorHandler {
  // 0x4CB910 (747 B). Installed by InitCrashReporter through
  // crashy::SetExceptionHandler; ECX is the exception info and EDX the
  // registered argument, which it never reads.
  static int ExceptionHandler(crashy::ExceptionInfo *pInfo, void *pvParam);

  // 0x4CBE00 (886 B). ECX is the report. Writes the identity header and the
  // process lines, attaches the log files, and answers false only when the
  // report is not open. Both callers discard AL.
  static bool SendBlizzardErrorReport(crashy::Report *pReport);
};

#endif  // OC117_ERRORHANDLER_H
