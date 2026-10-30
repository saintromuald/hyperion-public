// net_strt.cpp -- the multiplayer start countdown: a one-second Win32 timer
// that ticks a seconds byte down to zero and then hands off. Bodies are in
// oracle VA order. 1.17 inlines end_countdown into one of countdown_cb's arms
// and calls it from the other, so the two stay in one TU.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/net_strt.h"
#include "../h/uiMulti.h"     // update_countdown, sub_4BB320
#include "../gen/globals117.h"
#include "../port/platform/port_winmsg.h"  // Hyperion: SetTimer and KillTimer, the window's

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x673F10 -- set to 1 when the countdown starts and cleared by net_strt_init,
// a full dword.
#ifdef OC_DLL_MODE
#define sgNetCountdownOn_g (*reinterpret_cast<int *>(OC_VA(0x673F10u)))
#else
extern int sgNetCountdownOn_g;
#endif

// 0x673F14 -- the timer id SetTimer returns and KillTimer is given.
#ifdef OC_DLL_MODE
#define sgNetCountdownTimer_g (*reinterpret_cast<UINT_PTR *>(OC_VA(0x673F14u)))
#else
extern UINT_PTR sgNetCountdownTimer_g;
#endif

// 0x673F18 -- the finished flag, a BYTE.
#ifdef OC_DLL_MODE
#define sgbNetCountdownDone_g (*reinterpret_cast<BYTE *>(OC_VA(0x673F18u)))
#else
extern BYTE sgbNetCountdownDone_g;
#endif

// 0x673F19 -- the seconds remaining, a BYTE.
#ifdef OC_DLL_MODE
#define sgbNetCountdownSecs_g (*reinterpret_cast<BYTE *>(OC_VA(0x673F19u)))
#else
extern BYTE sgbNetCountdownSecs_g;
#endif

// The bodies, in oracle VA order.

// 0x47A140 (18 B). The byte is cleared before the dword.
void net_strt_init()
{
  sgbNetCountdownDone_g = 0;
  sgNetCountdownOn_g = 0;
}

// 0x47A160 (53 B). Arms a one-second timer and shows the first tick. The caller
// sets ECX, which this body never reads. The seconds byte is read back from the
// global for the tail call.
void begin_countdown(int nArg0)
{
  sgNetCountdownOn_g = 1;
  sgbNetCountdownSecs_g = 5;
  sgNetCountdownTimer_g = WinMsgSetTimer(NULL, 0, 1000, reinterpret_cast<WinMsgTimerProc>(countdown_cb));
  update_countdown(sgbNetCountdownSecs_g);
}

// 0x47A1A0 (108 B). The tick. Anything but the in-game chat room state tears
// the timer down; otherwise the seconds byte steps to zero and the last tick
// hands off. `ret 10h`: it is a Win32 TIMERPROC.
void __stdcall countdown_cb(HWND hWnd, UINT uMsg, UINT_PTR idEvent,
                            DWORD dwTime)
{
  if (gwGameMode == 4 && glGluesMode == 3) {
    const BYTE bSecs = sgbNetCountdownSecs_g;

    if (bSecs == 0) {
      end_countdown();
      sgbNetCountdownDone_g = 1;
      return;
    }

    sgbNetCountdownSecs_g = static_cast<BYTE>(bSecs - 1);
    update_countdown(sgbNetCountdownSecs_g);
    return;
  }

  end_countdown();
}

// 0x47A210 (54 B). Returns whether a timer was running, though both callers
// discard it.
BOOL end_countdown()
{
  if (sgNetCountdownTimer_g == 0)
    return FALSE;

  sub_4BB320();
  WinMsgKillTimer(NULL, sgNetCountdownTimer_g);
  sgNetCountdownTimer_g = 0;
  sgbNetCountdownSecs_g = 0;
  return TRUE;
}
