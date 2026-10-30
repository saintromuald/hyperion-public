// gds_appcrit.cpp -- the critical error path: the formatted fatal-error report,
// the orderly teardown that precedes it, and the crash reporter installed at
// startup. Bodies are in oracle VA order.
//
// Hyperion: the crash reporter is gone: the
// exception handler, Storm's error callback, the report's header and what
// only they used, with crashy and DebugHelp, which are out of the build. The
// port's own reporter (port_crash.cpp) stays. A fatal error still goes to the log.
//
// vappcritf has app_cleanup inlined, as in 1.17, so the two stay in this file
// together.

#include "oc_data.h"  // Hyperion: OC_VA
#include <windows.h>
#include <stdio.h>    // _vsnprintf; crashy.h brought it before step 5

#include "../h/gds_appcrit.h"
#include "../h/gds_log.h"
#include "../h/gds_appexi.h"
#include "../port/tools/port_crash.h"  // Hyperion: PortCrashCritical
#include "../h/Storm.h"
#include "../h/gds_vidinimo_PC.h"
#include "../gen/globals117.h"
#include "../port/tools/port_crash.h"  // Hyperion: PortInstallUnhandledFilter

// A callee no header of ours declares, spelled exactly as init.cpp spells it.
void __fastcall snd_close();           // 0x49EAE0, snd.cpp

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// gnThreadID, 0x6D1658: the thread that owns the game. Both teardown paths skip
// the device releases on any other thread.
#ifdef OC_DLL_MODE
#define gnThreadID (*reinterpret_cast<DWORD *>(OC_VA(0x6D1658u)))
#else
extern DWORD gnThreadID;
#endif


// gszCritMsg, 0x6D1458: the formatted message, 0x200 bytes up to gnThreadID,
// built once and then written to the log.
#ifdef OC_DLL_MODE
#define gszCritMsg (reinterpret_cast<char *>(OC_VA(0x6D1458u)))
#else
extern char gszCritMsg[0x200] OC_UNALIGNED;
#endif

// The bodies, in oracle VA order.

// 0x4CB890 (5 B). A deliberate null write, so the installed filter sees a real
// access violation. The null is a named pointer, which 1.17 loads into a
// register first.
void crash()
{
  int *pNull = 0;

  *pNull = 0;
}

// Hyperion: FindParentProcessId (0x4CB8A0), ErrorHandler::ExceptionHandler
// (0x4CB910), BlizzErrFunc (0x4CBC00), ErrorHandler::SendBlizzardErrorReport
// (0x4CBE00) and writestring_cb (0x4CC180) were the crash reporter's, which is
// gone; an address of theirs in 1.17's data goes to PortOriginalCodeReached.

// 0x4CC190 (14 B). Hands its varargs to vappcritf, which does not return.
void __cdecl appcritf(const char *pszFmt, ...)
{
  va_list args;

  va_start(args, pszFmt);
  vappcritf(pszFmt, args);
}

// 0x4CC1A0 (240 B). Records the owning thread, starts the crash reporter and
// hands it the install directory, then installs both error callbacks.
// Hyperion: the owning thread, and the port's unhandled-exception filter
// in place of the game's reporter, which is gone.
void InitCrashReporter()
{
  gnThreadID = GetCurrentThreadId();
  PortInstallUnhandledFilter();
}

// 0x4CC290 (135 B). /Ob2 inlines app_cleanup here, as 1.17 has it.
void __fastcall vappcritf(const char *pszFormat, va_list args)
{
  _vsnprintf(gszCritMsg, 0x200, pszFormat, args);
  gszCritMsg[0x1FF] = 0;

  log_dump_computer_info();
  log_printf("%s", gszCritMsg);
  log_printf("\r\n");
  log_flush(1);

  // Hyperion: BlizzErrFunc, the crash reporter's, is gone; the message is in the log, and
  // Hyperion's start-up log or crash report says it, with a box
  PortCrashCritical(gszCritMsg);

  app_cleanup();

  app_fatal();
}

// 0x4CC320 (29 B). No epilogue: ExitProcess does not return, and neither does
// this. app_exit runs only on the owning thread because a worker thread reaching
// here has nothing of its own to release.
void app_fatal()
{
  if (GetCurrentThreadId() == gnThreadID)
    app_exit(1);
  ExitProcess(1);
}

// 0x4CC340 (44 B). snd_close is a tail jump inside the guard, not after it.
void app_cleanup()
{
  SErrSuppressErrors(TRUE);
  SNetLeaveGame(3);
  SNetDestroy();
  if (GetCurrentThreadId() == gnThreadID) {
    free_directx();
    snd_close();
  }
}
