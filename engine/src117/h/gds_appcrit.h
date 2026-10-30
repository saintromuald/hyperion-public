// gds_appcrit.h -- declarations for src117/lang/gds_appcrit.cpp, the critical
// error path: the formatted fatal-error report, the orderly teardown that
// precedes it, and the crash reporter installed at startup.
//
// Nothing here returns on the fatal path. vappcritf tail-jumps app_fatal and
// app_fatal ends in ExitProcess, so the whole chain from appcritf down never
// returns. Only vappcritf is declared noreturn, for appcritf's sake.

#ifndef OC117_GDS_APPCRIT_H
#define OC117_GDS_APPCRIT_H

#include <stdarg.h>   // va_list, for vappcritf

#include "platform.h"

// 0x4CB890. Writes through a null pointer on purpose, to hand the installed
// exception filter a real access violation.
void crash();

// 0x4CC190. __cdecl varargs; tail-jumps vappcritf with the format in ECX and
// its own first vararg's address in EDX. The .cpp-local declarations elsewhere
// spell it exactly this way.
void __cdecl appcritf(const char *pszFmt, ...);

// 0x4CC290. ECX is the format, EDX the vararg list; formats into gszCritMsg,
// logs it, tears the game down and hands it to BlizzErrFunc. Without the
// noreturn, appcritf pops its frame before the tail jump, where 1.17 keeps it.
__declspec(noreturn) void __fastcall vappcritf(const char *pszFormat, va_list args);

// 0x4CB8A0. ECX is a process id; walks the toolhelp snapshot for its entry and
// returns that entry's parent id, or 0 if the id is not in the snapshot.
DWORD FindParentProcessId(DWORD dwProcessId);

// 0x4CBC00. Storm's error callback, installed by InitCrashReporter: Storm's own
// SERRBLIZZERRFUNC. ECX is the message and EDX a second string it never reads;
// the stack words are the files to attach, their count, the source line and
// whether the error is fatal. Returns AL, which vappcritf discards.
bool __fastcall BlizzErrFunc(const char *pszMessage, const char *pszDetail,
                             const char *apszFiles[], unsigned int nFiles,
                             unsigned int nLine, bool bFatal);

// 0x4CC320. Noreturn: runs app_exit only on the thread that owns the game, then
// ExitProcess regardless.
void app_fatal();

// 0x4CC340. Suppresses Storm's error boxes, leaves and destroys the network
// game, and, only on the owning thread, releases DirectX and the sound device.
void app_cleanup();

// 0x4CC1A0. Called once, from WinMain.
void InitCrashReporter();

// 0x4CC180. The crashy::LOGPROC sink: ECX is the open crashy::Report and EDX
// the line, printed through "%s". Only its address is ever taken.
void writestring_cb(void *pContext, const char *pszLine);

// The CRT entry InitCrashReporter calls, in <stdlib.h>'s spelling.
extern "C" {
char *__cdecl _fullpath(char *pszFullPath, const char *pszPath, size_t cchFullPath);
}

// psapi's GetModuleFileNameExA, which ErrorHandler::SendBlizzardErrorReport
// reaches through the static stub at 0x4E8910 rather than an import slot, so
// it is declared without dllimport (<psapi.h> would rename it K32...). The
// handles are `void *` because not every includer has <windows.h>.
extern "C" {
DWORD __stdcall GetModuleFileNameExA(void *hProcess, void *hModule, char *pszFileName,
                                     DWORD nSize);
}

#endif  // OC117_GDS_APPCRIT_H
