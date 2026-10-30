// gds_appexi.cpp -- running the registered exit callbacks and leaving the
// process. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gds_appexi.h"
#include "../h/gds_appaddex.h"
#include "../h/gds_sys_Win.h"
#include "../h/gds_log.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// _app_exits -- 0x6D3178, spelled as in gds_appaddex.cpp.
#ifdef OC_DLL_MODE
#define _app_exits (*reinterpret_cast<APPEXITPROC **>(OC_VA(0x6D3178u)))
#else
extern APPEXITPROC *_app_exits;
#endif

// isAppExitCalled -- 0x6D317C, a byte latch.
#ifdef OC_DLL_MODE
#define isAppExitCalled (*reinterpret_cast<BYTE *>(OC_VA(0x6D317Cu)))
#else
extern BYTE isAppExitCalled;
#endif

// _exit_slot -- 0x50F2E8, a signed word: the loop runs down to -1.
#ifdef OC_DLL_MODE
#define _exit_slot (*reinterpret_cast<short *>(OC_VA(0x50F2E8u)))
#else
extern short _exit_slot;
#endif

// app_run_exit -- 0x4CF6B0. Runs every registered exit callback once, from the
// last slot down, then frees the table. Each callback receives bFatal in CL,
// which APPEXITPROC does not declare.
void app_run_exit(BYTE bFatal)
{
  if (!_app_exits || isAppExitCalled)
    return;
  isAppExitCalled = 1;
  for (; _exit_slot != -1; --_exit_slot) {
    APPEXITPROC *pSlot = &_app_exits[_exit_slot];
    APPEXITPROC pfnExit = *pSlot;
    if (pfnExit) {
      *pSlot = 0;
      reinterpret_cast<void(__fastcall *)(BYTE)>(pfnExit)(bFatal);
    }
  }
  SMemFree(_app_exits, "appexi.cpp", 50, 0);
  _app_exits = 0;
}

// app_exit -- 0x4CF730. Runs the exit callbacks, flushes the log and exits
// with bFatal as the process exit code.
void app_exit(BYTE bFatal)
{
  if (bFatal)
    SErrSuppressErrors(TRUE);
  app_run_exit(bFatal);
  log_flush(1);
  gds_exit(bFatal);
}
