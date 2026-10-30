// port_crash.h -- Hyperion's crash report (port_crash.cpp).
#ifndef PORT_CRASH_H
#define PORT_CRASH_H

#include "../../h/platform.h"  // DWORD, ULONG_PTR

// Hyperion's crash report (port_crash.cpp): an unhandled-exception filter that writes the
// report and ends the process, and the C library's handlers for the ends that are no fault.
// Hyperion.exe installs them as it starts; InitCrashReporter installs the filter again where the
// game's own reporter went in.
void __cdecl PortInstallCrashReporter(void);
void __cdecl PortInstallUnhandledFilter(void);
// A line for the crash report's "last notes" (every log's lines, port_log.cpp), kept in a ring
// of the last 64 that the report prints oldest first.
void __cdecl PortCrashNote(const char *psz);
// One of the session's notes for the report, by name, replacing the last of that name
// (port_crashnotes.cpp): the game being played, the games played.
void __cdecl PortCrashSession(const char *pszName, const char *pszText);
// 1.17's own error, as its dialog says it (gds_appfat.cpp): the dialog can be out of sight, and
// the game ends after it, so a report is written first; during the start the start fails.
void __cdecl PortCrashFatal(const char *pszText);
// 1.17's critical error, which it ended on with no dialog (gds_appcrit.cpp's vappcritf).
void __cdecl PortCrashCritical(const char *pszText);
// HYPERION_TEST_CRASH=<kind>[:<where>] makes a crash of that kind here, for the report's tests:
// av, abort, terminate, throw, invalid or purecall; where is "menus" or "game".
void __cdecl PortCrashTest(const char *pszWhere);

// Another thread's call stack, a line a frame, for the start-up's watchdog (port_startup.cpp):
// its length, 0 where it cannot be read (off Windows).
int __cdecl PortCrashThreadStack(void *hThread, char *buf, int cb);

#if !defined(_WIN32)
// Off Windows, a fault the engine raises itself (port_posix.cpp's RaiseException): the report
// written as the handler writes one, and the program ended.
void __cdecl PortCrashRaise(DWORD dwCode, const ULONG_PTR *pArgs, DWORD nArgs);
#endif

#endif
