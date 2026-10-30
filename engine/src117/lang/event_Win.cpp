// event_Win.cpp -- the Win32 event layer (0x43F100 - 0x4405C0): the
// accelerator swap family, the window and message pump, the key-modifier
// reader and the screen capture. Bodies are in oracle VA order. init_events and
// accel_init both end with accel_set_sound inlined, as in 1.17.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/platform.h"
#include "../h/event_Win.h"
#include "../h/TrigAct.h"    // accel_set_sound / accel_restore / accel_save
#include "../h/dlg_dlg.h"    // RunMsgDispatcher, dlg_event, app_add_exit
#include "../h/save_dlg.h"   // RunEvtDispatcher, _mbsicmp
#include "../h/glues.h"      // CloseAllWindows
#include "../h/net_cmds.h"   // gameloop_init, error_get_str, map_msg_chat_msg
#include "../h/minimap.h"    // mou_cager
#include "../h/snd.h"        // snd_update_streams
#include "../h/victory.h"    // psnprintf
#include "../h/saveload.h"   // SearchDirectory, SEARCHDIR_CB
#include "../h/title.h"        // is_title_active
#include "../h/gds_vidblit.h"  // vid_paint_invalid
#include "../h/gds_mou_win.h"  // sub_4CE820
#include "../gen/globals117.h"
#include "../port/tools/port_benchview.h"  // Hyperion: PortBenchViewWheel
#include "../port/tools/port_control.h"  // Hyperion: PortControlPlayerInput, PortControlKeepsRunning
#include "../port/tools/port_debugview.h"  // Hyperion: PortDebugViewAvailable, PortDebugViewCycle
#include "../port/front/port_front.h"  // Hyperion: PortFrontWndMsg
#include "../port/render/port_gpu_hd.h"  // Hyperion: PortGpuHdToggle
#include "../port/gameplay/port_hotkeys.h"  // Hyperion: PortAccelRemap, PortAccelProfiled
#include "../port/gameplay/port_keys.h"  // Hyperion
#include "../port/loop/port_perf.h"  // Hyperion: PortPerfIdle
#include "../port/render/port_overlay.h"  // Hyperion: PortOverlayHold, PortOverlayMouse, PortOverlayKey
#include "../port/render/port_present.h"  // Hyperion
#include "../port/replays/port_tv.h"  // Hyperion: PortFollowToggle, PortTvToggle
#include "../port/render/port_view.h"  // Hyperion
#include "../port/lan/port_lanfeed.h"  // Hyperion: PortLanWatchLive
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudMouse
#include "../port/hud/port_hudstate.h"  // Hyperion: PortHudPressButton
#include "../h/selectlimits.h"          // Hyperion: SelectionWide, SelectionSubgroupStep

// Hyperion: 0x6BE658 gbInMsgMode (textbox.cpp), set while a message is typed
#ifdef OC_DLL_MODE
  #ifndef gbInMsgMode
  #define gbInMsgMode (*reinterpret_cast<BYTE *>(OC_VA(0x6BE658u)))
  #endif
#else
extern BYTE gbInMsgMode;
#endif
// Hyperion: 0x6CA720 gbSelectDragging (usel.cpp), set while a selection box is dragged
#ifdef OC_DLL_MODE
  #ifndef gbSelectDragging
  #define gbSelectDragging (*reinterpret_cast<BYTE *>(OC_VA(0x6CA720u)))
  #endif
#else
extern BYTE gbSelectDragging;
#endif

// The Win32 and IMM32 imports, declared here and not in event_Win.h, where they
// would collide with <windows.h>'s prototypes. Handles are `void *`. The
// KERNEL32 ones are dllimport, so that each call goes through its IAT slot as
// in 1.17.
extern "C" __declspec(dllimport) WORD  __stdcall GetUserDefaultLangID(void);      // 0x4EB25C
extern "C" __declspec(dllimport) void  __stdcall GetLocalTime(void *pSystemTime); // 0x4EB260
extern "C" __declspec(dllimport) void  __stdcall Sleep(DWORD dwMilliseconds);     // 0x4EB2F8
extern "C" __declspec(dllimport) DWORD __stdcall GetTickCount(void);              // 0x4EB2FC

// USER32: the accelerator tables, the message pump, the cursor, capture and the
// keyboard. In Hyperion the window is SDL's and these are port_winmsg.cpp's,
// the messages made from SDL's events.
#include "../port/platform/port_winmsg.h"

// IMM32, without dllimport: 1.17 calls both by a direct call into the import
// stubs at 0x4BE964 and 0x4BE970, and the link takes them from imm32.lib.
// hWnd comes first, then the context.
extern "C" void *__stdcall ImmGetContext(void *hWnd);                    // 0x4BE964
extern "C" void *__stdcall ImmAssociateContext(void *hWnd, void *hIMC);  // 0x4BE970

// The TU-local globals, dual-spelled, in address order; gen/globals117.h emits
// none of them. The widths are 1.17's. gKeyTbl at 0x665E58 is 0x100 bytes, as
// RunEvtDispatcher's memset clears it, and 0x665E68 to 0x665E6A are its
// VK_SHIFT, VK_CONTROL and VK_MENU entries, spelled as usel.cpp spells them.

// The event-handler table at 0x665DF0, typed as usel.cpp, game_evt.cpp and
// playflic.cpp type it.
typedef void(__fastcall *PFN_EVT_HOOK)(dlgevt_t *pEvent);

#ifdef OC_DLL_MODE
#define sgbShowingCursor_g (*reinterpret_cast<int *>(OC_VA(0x5032ACu)))
#define gAccelCur          (*reinterpret_cast<void **>(OC_VA(0x665DE8u)))
#define gEventHooks        (reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x665DF0u)))
#define sgbMouseUpdated_l  (*reinterpret_cast<DWORD *>(OC_VA(0x665E3Cu)))
#define gAccelSingle       (*reinterpret_cast<void **>(OC_VA(0x665E40u)))
#define gAccelMulti        (*reinterpret_cast<void **>(OC_VA(0x665E44u)))
#define gAccelSound        (*reinterpret_cast<void **>(OC_VA(0x665E48u)))
#define gAccelSaved        (*reinterpret_cast<void **>(OC_VA(0x665E4Cu)))
#define gAccelProcSaved    (*reinterpret_cast<PFN_ACCELPROC *>(OC_VA(0x665E50u)))
#define gKeyTbl            (reinterpret_cast<BYTE *>(OC_VA(0x665E58u)))
#define byte_665E68        (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
#define byte_665E69        (*reinterpret_cast<BYTE *>(OC_VA(0x665E69u)))
#define byte_665E6A        (*reinterpret_cast<BYTE *>(OC_VA(0x665E6Au)))
#define s_activeHCursor    (*reinterpret_cast<void **>(OC_VA(0x665F60u)))
#define ghImmContext_g     (*reinterpret_cast<void **>(OC_VA(0x665F64u)))
#define dword_665F6C       (*reinterpret_cast<DWORD *>(OC_VA(0x665F6Cu)))
#define dword_665F70       (*reinterpret_cast<DWORD *>(OC_VA(0x665F70u)))
#define killedmom          (*reinterpret_cast<int *>(OC_VA(0x665F74u)))
#define killedindexer      (*reinterpret_cast<int *>(OC_VA(0x665F78u)))
#define sgbWindowActive_l  (*reinterpret_cast<DWORD *>(OC_VA(0x66FD24u)))
#define ghWnd              (*reinterpret_cast<void **>(OC_VA(0x66FD28u)))
#define isCPUThrottleEnabled       (*reinterpret_cast<DWORD *>(OC_VA(0x6AAA64u)))
#define gwMouseFlags_l     (*reinterpret_cast<WORD *>(OC_VA(0x6D315Cu)))
#else
extern int sgbShowingCursor_g;
extern void *gAccelCur;
extern PFN_EVT_HOOK gEventHooks[19];
extern DWORD sgbMouseUpdated_l;
extern void *gAccelSingle;
extern void *gAccelMulti;
extern void *gAccelSound;
extern void *gAccelSaved;
extern PFN_ACCELPROC gAccelProcSaved;
extern BYTE gKeyTbl[0x100] OC_UNALIGNED;
extern BYTE byte_665E68;
extern BYTE byte_665E69;
extern BYTE byte_665E6A;
extern void *s_activeHCursor;
extern void *ghImmContext_g;
extern DWORD dword_665F6C;
extern DWORD dword_665F70;
extern int killedmom;
extern int killedindexer;
extern DWORD sgbWindowActive_l;
extern void *ghWnd;
extern DWORD isCPUThrottleEnabled;
extern WORD gwMouseFlags_l;
#endif

// gAccelProc at 0x665E30 is gEventHooks[0x10], the accelerator hook, which
// init_events' memset of the table clears with the others. It is spelled as the
// slot because the table's layout changes at 64 bits (7b).
#define gAccelProc (gEventHooks[0x10])
#ifdef OC_DLL_MODE
#define gpfnEvtCleanup     (*reinterpret_cast<void (**)()>(OC_VA(0x665DECu)))
#else
extern void (*gpfnEvtCleanup)();
#endif

extern "C" void *__cdecl memset(void *pDst, int nVal, size_t cb);  // Hyperion: size_t

// One accelerator-table entry, Win32's ACCEL at default alignment: 6 bytes,
// with a hole after fVirt. accel_init's 0x600-byte buffer holds 0x100 of them.
struct accel_t
{
  /*0x00*/ BYTE fVirt;
  /*0x02*/ WORD wKey;
  /*0x04*/ WORD wCmd;
};
static_assert(sizeof(accel_t) == 6, "accel_t");

// SYSTEMTIME, for screen_capture, declared here because <windows.h> is kept off
// the include path.
struct systemtime_t
{
  /*0x00*/ WORD wYear;
  /*0x02*/ WORD wMonth;
  /*0x04*/ WORD wDayOfWeek;     // not read here
  /*0x06*/ WORD wDay;
  /*0x08*/ WORD wHour;
  /*0x0A*/ WORD wMinute;
  /*0x0C*/ WORD wSecond;
  /*0x0E*/ WORD wMilliseconds;  // not read here
};
static_assert(offsetof(systemtime_t, wYear) == 0x00, "systemtime_t.wYear");
static_assert(offsetof(systemtime_t, wMonth) == 0x02, "systemtime_t.wMonth");
static_assert(offsetof(systemtime_t, wDayOfWeek) == 0x04, "systemtime_t.wDayOfWeek");
static_assert(offsetof(systemtime_t, wDay) == 0x06, "systemtime_t.wDay");
static_assert(offsetof(systemtime_t, wHour) == 0x08, "systemtime_t.wHour");
static_assert(offsetof(systemtime_t, wMinute) == 0x0A, "systemtime_t.wMinute");
static_assert(offsetof(systemtime_t, wSecond) == 0x0C, "systemtime_t.wSecond");
static_assert(offsetof(systemtime_t, wMilliseconds) == 0x0E, "systemtime_t.wMilliseconds");
static_assert(sizeof(systemtime_t) == 0x10, "systemtime_t");

// MSG, for RunEvtDispatcher, which only passes it on by address.
// Hyperion: wParam and lParam are pointer-wide, as in MSG, so that at 64 bits
// the struct is MSG's 0x30 bytes, which PeekMessageA writes; on x86 they are
// the same 32 bits.
// ---------------------------------------------------------------------------
struct win_msg_t {
  void *hwnd;
  unsigned int message;
  uintptr_t wParam;
  win_lparam_t lParam;
  DWORD time;
  LONG ptX;
  LONG ptY;
};
static_assert(sizeof(win_msg_t) == (sizeof(void *) == 8 ? 0x30 : 0x1C), "win_msg_t is MSG");
static_assert(sizeof(win_msg_t) == sizeof(WinMsg) && offsetof(win_msg_t, lParam) == offsetof(WinMsg, lParam) &&
              offsetof(win_msg_t, ptY) == offsetof(WinMsg, nPtY), "win_msg_t is port_winmsg.h's WinMsg");

// The event-handler table at 0x665DF0, gEventHooks, read as 19 fastcall
// handlers indexed by dlgevt_t::wTag.
typedef void (*PFN_EVTHANDLER)(dlgevt_t *pEvent);
#define EVT_HANDLER(i) (gEventHooks[i])

// The default handler the three Win-event bodies call when no dialog takes the
// event: one register argument, the event.
typedef int (*PFN_WINEVT)(dlgevt_t *pEvent);

// 0x43F100 accel_set_sound (21 B). Makes the sound table current, with
// game_mute_key_hook as the handler.
void accel_set_sound()
{
  gAccelCur = gAccelSound;
  gAccelProc = game_mute_key_hook;
}

// 0x43F120 accel_set_multi (21 B)
void accel_set_multi()
{
  gAccelCur = gAccelMulti;
  gAccelProc = dialog_accel;
}

// 0x43F140 accel_set_single (21 B)
void accel_set_single()
{
  gAccelCur = gAccelSingle;
  gAccelProc = dialog_accel;
}

// 0x43F160 accel_restore (21 B). Both stores come from loads.
void accel_restore()
{
  gAccelCur = gAccelSaved;
  gAccelProc = gAccelProcSaved;
}

// 0x43F180 accel_save (21 B) -- accel_restore run backwards.
void accel_save()
{
  gAccelSaved = gAccelCur;
  gAccelProcSaved = gAccelProc;
}

// 0x43F1A0 accel_close (105 B). Destroys the three tables, each clear inside its
// guard, then clears the saved state.
void accel_close()
{
  if (gAccelSingle)
  {
    WinMsgDestroyAccelerators(gAccelSingle);
    gAccelSingle = 0;
  }
  if (gAccelMulti)
  {
    WinMsgDestroyAccelerators(gAccelMulti);
    gAccelMulti = 0;
  }
  if (gAccelSound)
  {
    WinMsgDestroyAccelerators(gAccelSound);
    gAccelSound = 0;
  }
  gAccelSaved = 0;
  gAccelProcSaved = 0;
  gAccelCur = 0;
}

// 0x43F210 accel_init (279 B). Builds the single-player accelerator table by
// concatenating resource 0x65 with resource 0x71, falling back to plain 0x65 if
// any step fails. The running total is unsigned, as 1.17's compare is.
void accel_init()
{
  accel_t buf[0x100];
  void *hSingle;
  void *hMulti;
  unsigned int nSingle;
  unsigned int nMulti;
  unsigned int nTotal;

  gAccelSingle = 0;
  hSingle = GetAcceleratorTable(0x65);
  hMulti = GetAcceleratorTable(0x71);
  if (hSingle)
  {
    if (hMulti)
    {
      nSingle = WinMsgCopyAccelerators(hSingle, 0, 0);
      nMulti = WinMsgCopyAccelerators(hMulti, 0, 0);
      nTotal = nSingle + nMulti;
      if (nTotal <= 0x100)
      {
        // Each count is the left operand of its compare, as in 1.17.
        if (nSingle == WinMsgCopyAccelerators(hSingle, buf, nSingle)
            && nMulti == WinMsgCopyAccelerators(hMulti, &buf[nSingle], nMulti))
        {
          gAccelSingle = WinMsgCreateAccelerators(buf, nTotal);
        }
      }
    }
    WinMsgDestroyAccelerators(hSingle);
  }
  if (hMulti)
    WinMsgDestroyAccelerators(hMulti);

  if (!gAccelSingle)
    gAccelSingle = GetAcceleratorTable(0x65);
  gAccelMulti = GetAcceleratorTable(0x66);
  gAccelSound = GetAcceleratorTable(0x67);
  gAccelSaved = 0;
  gAccelProcSaved = 0;
  accel_set_sound();
}

// Hyperion: the profile's keys for the accelerators' commands (the hotkeys milestone
// K2, port_hotkeys.cpp), as a game starts: the three tables built again from 1.17's entries,
// which the first time keeps, and every place that held an old table given the new one. A
// profile that binds none leaves 1.17's tables, and so does a later one after them.
static void AccelRebuild(void **ppTable, const accel_t *pKept, int nKept)
{
  accel_t buf[0x100];
  const int n = PortAccelRemap(pKept, nKept, buf, 0x100);
  void *hOld = *ppTable;
  void *hNew = n > 0 ? WinMsgCreateAccelerators(buf, n) : 0;
  *ppTable = hNew;
  if (gAccelCur == hOld)
    gAccelCur = hNew;
  if (gAccelSaved == hOld)
    gAccelSaved = hNew;
  if (hOld)
    WinMsgDestroyAccelerators(hOld);
}

void accel_apply_profile()
{
  static accel_t s_single[0x100], s_multi[0x100], s_sound[0x100];
  static int s_nSingle = -1, s_nMulti, s_nSound;
  if (s_nSingle < 0) {
    if (!PortAccelProfiled())
      return;
    s_nSingle = gAccelSingle ? WinMsgCopyAccelerators(gAccelSingle, s_single, 0x100) : 0;
    s_nMulti = gAccelMulti ? WinMsgCopyAccelerators(gAccelMulti, s_multi, 0x100) : 0;
    s_nSound = gAccelSound ? WinMsgCopyAccelerators(gAccelSound, s_sound, 0x100) : 0;
  }
  AccelRebuild(&gAccelSingle, s_single, s_nSingle);
  AccelRebuild(&gAccelMulti, s_multi, s_nMulti);
  AccelRebuild(&gAccelSound, s_sound, s_nSound);
}

// 0x43F330 init_events (66 B). Runs and clears the cleanup hook, clears the
// 0x4C-byte handler table and ends with accel_set_sound inlined.
void init_events()
{
  // The clear is inside the guard.
  if (gpfnEvtCleanup)
  {
    gpfnEvtCleanup();
    gpfnEvtCleanup = 0;
  }
  memset(gEventHooks, 0, sizeof(PFN_EVT_HOOK) * 19);
  mou_cager(0);
  accel_set_sound();
}

// 0x43F380 show_cursor (97 B). Shows or hides the cursor.
void show_cursor(int bShow)
{
  // Hyperion: the window is SDL's (port_winmsg.cpp). The oracle set
  // Windows' arrow or no cursor over its window, and moved the cursor where it was
  // so that Windows showed the change; the game draws its own cursor, and SDL hides
  // the system's over the window (port_present.cpp), so only the count is kept.
  if (sgbShowingCursor_g != bShow)
  {
    sgbShowingCursor_g = bShow;
    WinMsgShowCursor(bShow);
  }
}

// 0x43F3F0 init_lang_imm (83 B). Associates the Korean IME with the window, or
// takes it away, when the user's language is ko-KR (0x412); only the else-arm
// stores the context back.
void init_lang_imm(int bEnable)
{
#if defined(_WIN32)
  if (GetUserDefaultLangID() != 0x412)
    return;

  void *hImc = ghImmContext_g;
  if (hImc == 0)
  {
    hImc = ImmGetContext(ghWnd);
    ghImmContext_g = hImc;
  }
  if (bEnable == 1)
    ImmAssociateContext(ghWnd, hImc);
  else
    ghImmContext_g = ImmAssociateContext(ghWnd, 0);
#else
  (void)bEnable;  // no Korean input method off Windows yet
#endif
}

// 0x43F450 WinEvtCaptureChanged (44 B). Drops the held buttons on a capture
// change and resets the handlers. The flags are masked in place, with no local,
// which keeps 1.17's two tail jumps.
void WinEvtCaptureChanged()
{
  if (gwMouseFlags_l & 0x2A)
  {
    gwMouseFlags_l &= 0xFFD5;
    if (gwGameMode == 3)
      game_reset_handlers();
    else
      init_events();
  }
}

// WinEvtButtonDown and WinEvtButtonUp share their coordinate tail: the packed
// lParam's low word is X and its high word Y, each clamped by an unsigned
// compare and stored sign-extended into the cursor globals. X is assigned to the
// event field in both arms, and Y goes through a local with the global stored
// first, as in 1.17. dwId, dwParam and _unmapped08 stay unwritten, as in 1.17.

// 0x43F480 WinEvtButtonDown (150 B). Sets the button's bit and captures the
// mouse, unless a held button blocks it. The tested value has a local of its
// own, as in 1.17.
void WinEvtButtonDown(short sButton, WORD wTag, intptr_t pfnDefault, int lParam)
{
  dlgevt_t evt;
  WORD wHeld = (WORD)(~sButton & gwMouseFlags_l);

  if ((wHeld & 0x2A) != 0)
    return;
  gwMouseFlags_l = (WORD)(gwMouseFlags_l | sButton);
  WinMsgSetCapture(ghWnd);

  evt.wTag = wTag;

  // Hyperion: clamped to the view's screen, 640x480 in 1.17.
  WORD wX = (WORD)lParam;
  if (wX >= g_portView.nWidth)
    evt.nX = (short)(g_portView.nWidth - 1);
  else
    evt.nX = (short)wX;
  gCursorPos_g[0] = evt.nX;

  WORD wY = (WORD)((DWORD)lParam >> 16);
  short nY;
  if (wY >= g_portView.nHeight)
    nY = (short)(g_portView.nHeight - 1);
  else
    nY = (short)wY;
  gCursorPos_g[1] = nY;
  evt.nY = nY;

  // Hyperion: the HUD's panels are the console (port_hudinput.cpp)
  if (PortHudMouse(&evt))
    return;
  if (dlg_event(&evt) == 0 && pfnDefault != 0)
    reinterpret_cast<PFN_WINEVT>(pfnDefault)(&evt);
}

// 0x43F520 WinEvtButtonUp (135 B). Clears the button's bit and releases the
// capture. ECX is read as 32 bits here and as 16 in WinEvtButtonDown; the stored
// value is derived again from the global, as in 1.17.
void WinEvtButtonUp(int nButton, WORD wTag, intptr_t pfnDefault, int lParam)
{
  dlgevt_t evt;
  WORD wFlags = (WORD)(~nButton & gwMouseFlags_l);

  if ((wFlags & 0x2A) != 0)
    return;
  gwMouseFlags_l = (WORD)(~nButton & gwMouseFlags_l);
  WinMsgReleaseCapture();

  evt.wTag = wTag;

  // Hyperion: clamped to the view's screen, 640x480 in 1.17.
  WORD wX = (WORD)lParam;
  if (wX >= g_portView.nWidth)
    evt.nX = (short)(g_portView.nWidth - 1);
  else
    evt.nX = (short)wX;
  gCursorPos_g[0] = evt.nX;

  WORD wY = (WORD)((DWORD)lParam >> 16);
  short nY;
  if (wY >= g_portView.nHeight)
    nY = (short)(g_portView.nHeight - 1);
  else
    nY = (short)wY;
  gCursorPos_g[1] = nY;
  evt.nY = nY;

  // Hyperion: the HUD's panels are the console (port_hudinput.cpp)
  if (PortHudMouse(&evt))
    return;
  if (dlg_event(&evt) == 0 && pfnDefault != 0)
    reinterpret_cast<PFN_WINEVT>(pfnDefault)(&evt);
}

// 0x43F5B0 WinEvtMouseWheel (65 B). The one body of the three that fills the
// event in full, and the only place dlgevt_t +0x08 is written. ECX is never read.
void WinEvtMouseWheel(int nUnusedEcx, WORD wTag, intptr_t pfnDefault, int lParam)
{
  dlgevt_t evt;

  (void)nUnusedEcx;
  evt.wTag = wTag;
  evt.dwId = 0;
  evt.dwParam = (dlg_dword_t)lParam;
  *reinterpret_cast<dlg_dword_t *>(evt._unmapped08) = 0;
  evt.nX = 0;
  evt.nY = 0;

  if (dlg_event(&evt) == 0 && pfnDefault != 0)
    reinterpret_cast<PFN_WINEVT>(pfnDefault)(&evt);
}

// The imports and globals WndProc reaches.
// Hyperion: BeginPaint 0x4EB684, EndPaint 0x4EB680, DefWindowProcA 0x4EB69C,
// FindWindowA 0x4EB65C and SetFocus 0x4EB698 were the oracle's here; the window is
// SDL's and its default procedure port_winmsg.cpp's.
extern "C" LONG __stdcall ImmGetCompositionStringA(void *hIMC, DWORD dwIndex, void *lpBuf,
                                                   DWORD dwBufLen);                            // 0x4BE976
extern "C" BOOL __stdcall ImmReleaseContext(void *hWnd, void *hIMC);                           // 0x4BE96A
extern "C" BOOL __stdcall ImmSetCompositionStringA(void *hIMC, DWORD dwIndex, void *lpComp,
                                                   DWORD dwCompLen, void *lpRead,
                                                   DWORD dwReadLen);                           // 0x4BE97C
extern "C" BOOL __stdcall SNetLeagueLogout(char *pszName);                                     // 0x4D0088
extern "C" void *__cdecl malloc(size_t cb);  // Hyperion: size_t
extern "C" void __cdecl free(void *pv);

#ifdef OC_DLL_MODE
#define dword_665F58 (*reinterpret_cast<DWORD *>(OC_VA(0x665F58u)))
#define dword_665F5C (*reinterpret_cast<DWORD *>(OC_VA(0x665F5Cu)))
#define dword_665F68 (*reinterpret_cast<DWORD *>(OC_VA(0x665F68u)))
#else
extern DWORD dword_665F58;
extern DWORD dword_665F5C;
extern DWORD dword_665F68;
#endif

#define EVT_SLOT(i) (reinterpret_cast<intptr_t>(gEventHooks[i]))
#define EVT_KEY(e) (*reinterpret_cast<WORD *>((e)._unmapped08))
#define EVT_MODS(e) (*reinterpret_cast<WORD *>((e)._unmapped08 + 2))

// The Korean IME composition handler behind the language and busy checks,
// inlined into WndProc's case 0x10F. malloc and free are our CRT's, so both
// ends use one heap.
static __forceinline win_lparam_t WndProcImeBody(void *hWnd, unsigned int uMsg, uintptr_t wParam,
                                                 win_lparam_t lParam, dlgevt_t &evt)
{
  void *hImc = ImmGetContext(hWnd);
  unsigned int cb;
  BYTE *pBuf;

  if (lParam & 0x800) {
    cb = ImmGetCompositionStringA(hImc, 0x800, 0, 0);
    pBuf = static_cast<BYTE *>(malloc(cb));
    ImmGetCompositionStringA(hImc, 0x800, pBuf, cb);
  } else if (lParam & 8) {
    cb = ImmGetCompositionStringA(hImc, 8, 0, 0);
    pBuf = static_cast<BYTE *>(malloc(cb));
    ImmGetCompositionStringA(hImc, 8, pBuf, cb);
  } else {
    ImmReleaseContext(hWnd, hImc);
    return WinMsgDefProc(hWnd, uMsg, wParam, lParam);
  }
  ImmReleaseContext(hWnd, hImc);

  if (dword_665F68 == 1) {
    evt.wTag = 0x0F;
    EVT_MODS(evt) = 0xA0;
    EVT_KEY(evt) = 8;
    if (!dlg_event(&evt) && EVT_HANDLER(0x0F))
      EVT_HANDLER(0x0F)(&evt);
    dword_665F68 = 0;
  }
  if (cb >= 2) {
    dword_665F58 = 1;
    dword_665F5C = 0;
  }
  for (unsigned int i = 0; i < cb; i++) {
    evt.wTag = 0x0F;
    EVT_KEY(evt) = pBuf[i];
    EVT_MODS(evt) = 0xA0;
    if (!dlg_event(&evt) && EVT_HANDLER(0x0F))
      EVT_HANDLER(0x0F)(&evt);
    if (dword_665F5C == 1) {
      ImmSetCompositionStringA(hImc, 9, 0, 0, 0, 0);
      if (i == 1) {
        evt.wTag = 0x0F;
        EVT_MODS(evt) = 0xA0;
        EVT_KEY(evt) = 8;
        if (!dlg_event(&evt) && EVT_HANDLER(0x0F))
          EVT_HANDLER(0x0F)(&evt);
        dword_665F68 = 0;
      }
      cb = 0;
      break;
    }
  }
  if (lParam & 8)
    dword_665F68 = cb ? 1 : dword_665F68;
  free(pBuf);
  ImmReleaseContext(hWnd, hImc);
  return 1;
}

// Case 0x10F: other languages go to DefWindowProcA, and while a button is held
// it answers 1.
static __forceinline win_lparam_t WndProcImeComposition(void *hWnd, unsigned int uMsg, uintptr_t wParam,
                                                        win_lparam_t lParam, dlgevt_t &evt)
{
  if (GetUserDefaultLangID() != 0x412)
    return WinMsgDefProc(hWnd, uMsg, wParam, lParam);
  win_lparam_t lr = 1;
  if (!(gwMouseFlags_l & 0x2A))
    lr = WndProcImeBody(hWnd, uMsg, wParam, lParam, evt);
  return lr;
}

// 0x43F600 (2112 B). The main window procedure: paints, activation, keyboard,
// mouse and IME messages become dlgevt_t events; everything else goes to
// DefWindowProcA. Handled messages break to the one trailing `return 1`, and
// case 0x0F comes before 0x10, as 1.17's blocks are ordered.
win_lparam_t __stdcall WndProc(void *hWnd, unsigned int uMsg, uintptr_t wParam, win_lparam_t lParam)
{
  dlgevt_t evt;

  // Hyperion: while Hyperion's front end is up (port_front.cpp), it takes the
  // window's mouse and keys, in the window's own pixels.
  LONG_PTR lFront;
  if (PortFrontWndMsg(hWnd, uMsg, wParam, lParam, &lFront))
    return lFront;

  // Hyperion: while a layer over the game holds it, a skirmish's countdown or a cutscene
  // (which Escape ends), the player's keys and buttons wait for it (port_overlay.h)
  if (PortOverlayHold(uMsg, static_cast<unsigned long long>(wParam), static_cast<long long>(lParam)))
    return 0;

  // Hyperion: a click or key of the player's ends a pause the live channel made
  // (port_control.cpp), which has no dialog to end it from
  if (uMsg == 0x201 || uMsg == 0x204 || uMsg == 0x100)  // WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_KEYDOWN
    PortControlPlayerInput();

  // Hyperion: the window is scaled, so a mouse message's client
  // position becomes a position in the 640x480 frame before the game reads it
  // (the oracle's window was the frame). A background or hidden window takes
  // no mouse input at all: the user's mouse passing over it reaches nothing.
  // Alt+Enter switches between full screen and a window, and its WM_SYSCHAR is
  // swallowed so it does not beep.
  if (uMsg >= 0x200 && uMsg <= 0x209) {
    if (PortMouseIgnored(&wParam))
      return 0;
    // Hyperion: over the panels of a layer over the game (Debug Tools', a benchmark run's,
    // the trainer's) the mouse is the layer's (port_overlay.h)
    if (uMsg == 0x200)
      PortCursorOffGame(static_cast<LONG>(lParam));
    if (PortOverlayMouse(uMsg, static_cast<unsigned long long>(wParam), static_cast<long long>(lParam)))
      return 0;
    // a click on the game takes back a cursor Ctrl+Alt let go
    if (PortCursorTakeBack(uMsg, static_cast<LONG>(lParam)))
      return 0;
    lParam = PortMouseToGame(lParam);
  }
  if (uMsg == 0x104 && wParam == 0x0D && (lParam & 0x20000000) != 0) {
    PortToggleFullscreen();
    return 0;
  }
  // Hyperion: Hyperion's hotkeys in a game (port_keys.cpp), function keys 1.17
  // leaves free: the art's, F5 as Remastered's, switches between classic and HD
  // (port_gpu.cpp, blending over half a second), and the view's, Ctrl+F5, between
  // 640x480 and the wide view (port_view.cpp); Settings can change either. A held key's
  // repeats do not switch it back and forth. With Alt it comes as WM_SYSKEYDOWN.
  // Hyperion: the keys of the layers over a game (port_overlay.h), a benchmark run's, a
  // watched LAN match's L and the trainer's, and the characters they make, which 1.17 would take
  // as its own
  if (PortOverlayKey(uMsg, static_cast<unsigned long long>(wParam), static_cast<long long>(lParam)))
    return 0;
  if ((uMsg == 0x100 || uMsg == 0x104) && gwGameMode == 3) {
    const int nHotkey = PortHotkeyPressed(static_cast<unsigned>(wParam));
    const bool bRepeat = (lParam & 0x40000000) != 0;
    // Ctrl+Alt (VK_CONTROL 0x11, VK_MENU 0x12), either first, lets the cursor go (port_present.cpp); the keys still reach the
    // game, which has no use for them alone
    if (!bRepeat && ((wParam == 0x11 && WinMsgKeyState(0x12) < 0) || (wParam == 0x12 && WinMsgKeyState(0x11) < 0)))
      PortCursorFreeSet(true);
    // the camera's: the zoom's keys repeat while held, the rest do not
    if (nHotkey == PORT_HOTKEY_ZOOM_OUT || nHotkey == PORT_HOTKEY_ZOOM_IN) {
      PortViewZoomKey(nHotkey == PORT_HOTKEY_ZOOM_OUT ? 1 : -1);
      return 0;
    }
    if (!bRepeat && nHotkey == PORT_HOTKEY_ZOOM_RESET) {
      PortViewZoomReset();
      return 0;
    }
    if (!bRepeat && nHotkey == PORT_HOTKEY_ZOOM_FIT) {
      PortViewZoomFit();
      return 0;
    }
    if (!bRepeat && nHotkey == PORT_HOTKEY_FOLLOW) {
      PortFollowToggle();
      return 0;
    }
    if (!bRepeat && nHotkey == PORT_HOTKEY_TV) {
      if (gPlayback)
        PortTvToggle();
      return 0;
    }
    if (!bRepeat && nHotkey == PORT_HOTKEY_DEBUG_VIEW && PortDebugViewAvailable()) {
      PortDebugViewCycle();
      return 0;
    }
  }
  // Hyperion: the selection's keys, not while a message is typed: the
  // subgroup's in a game of Hyperion's selection, with Shift the subgroup before; the terrain's,
  // 1.17's Tab, with Shift the units' colours; the idle worker's and the army's, as their
  // buttons. A held key does not repeat them.
  if ((uMsg == 0x100 || uMsg == 0x104) && gwGameMode == 3 && !gbInMsgMode) {
    const unsigned nHeld = PortHotkeyHeld(static_cast<unsigned>(wParam));
    const bool bRepeat = (lParam & 0x40000000) != 0;
    const unsigned nSubgroup = PortHotkey(PORT_HOTKEY_SUBGROUP);
    const unsigned nTerrain = PortHotkey(PORT_HOTKEY_TERRAIN);
    const auto Is = [nHeld](unsigned nKey) {
      return nKey != 0 && (nHeld == nKey || ((nKey & PORT_KEY_SHIFT) == 0 && nHeld == (nKey | PORT_KEY_SHIFT)));
    };
    if (SelectionWide() && Is(nSubgroup)) {
      if (!bRepeat)
        SelectionSubgroupStep(nHeld == nSubgroup ? 1 : -1);
      return 0;
    }
    if (Is(nTerrain)) {
      if (!bRepeat)
        PortMinimapTerrainKey();
      return 0;
    }
    const int nHotkey = PortHotkeyPressed(static_cast<unsigned>(wParam));
    if (nHotkey == PORT_HOTKEY_IDLE || nHotkey == PORT_HOTKEY_ARMY) {
      const char *pszWhy = NULL;
      if (!bRepeat)
        PortHudPressButton(nHotkey == PORT_HOTKEY_IDLE ? PORT_HUD_BTN_IDLE : PORT_HUD_BTN_ARMY, &pszWhy);
      return 0;
    }
  }
  if ((uMsg == 0x100 || uMsg == 0x104) && (lParam & 0x40000000) == 0 && gwGameMode == 3) {
    const int nHotkey = PortHotkeyPressed(static_cast<unsigned>(wParam));
    if (nHotkey == PORT_HOTKEY_ART) {
      PortGpuHdToggle();
      return 0;
    }
    if (nHotkey == PORT_HOTKEY_VIEW) {
      PortViewToggle();
      return 0;
    }
  }
  if (uMsg == 0x106 && wParam == 0x0D)
    return 0;

  switch (uMsg) {
  case 0x0F:
    // Hyperion: SDL's window has nothing to validate; the oracle's
    // BeginPaint and EndPaint pair is gone
    vid_paint_invalid();
    return 0;

  case 0x10:
    close_window();
    return 0;

  case 0x02:
    ghWnd = 0;
  default:
  defproc:
    return WinMsgDefProc(hWnd, uMsg, wParam, lParam);

  case 0x1C:
    sgbWindowActive_l = static_cast<DWORD>(wParam);  // WM_ACTIVATEAPP's BOOL
    show_cursor(wParam == 0);
    sub_4CE820((int)sgbWindowActive_l);
    memset(gKeyTbl, 0, 0x100);
    if (sgbWindowActive_l) {
      vid_paint_invalid();
      // Hyperion: the oracle gave the focus to a Storm dialog's window here
      // (gwGameMode 4, glGluesMode 4); Hyperion has none
    }
    goto defproc;

  case 0x86:
    if (wParam)
      memset(gKeyTbl, 0, 0x100);
    goto defproc;

  case 0x111:
    if (!gAccelProc)
      break;
    evt.wTag = 0x10;
    EVT_KEY(evt) = (WORD)wParam;
    gAccelProc(&evt);
    break;

  case 0x102:
    if (gwMouseFlags_l & 0x2A)
      break;
    evt.wTag = 0x0F;
    EVT_KEY(evt) = (WORD)wParam;
    EVT_MODS(evt) = (WORD)get_key_modifiers();
    dword_665F58 = 0;
    if ((WORD)((DWORD)lParam >> 16) & 0x4000)
      EVT_MODS(evt) |= 0x100;
    if (!dlg_event(&evt) && EVT_HANDLER(0x0F))
      EVT_HANDLER(0x0F)(&evt);
    break;

  case 0x10F:
    return WndProcImeComposition(hWnd, uMsg, wParam, lParam, evt);

  case 0x100:
  case 0x104:
    gKeyTbl[wParam] = 1;
    if (wParam == 0x90) {
      gKeyTbl[0x60] = 0;
      gKeyTbl[0x61] = 0;
      gKeyTbl[0x62] = 0;
      gKeyTbl[0x63] = 0;
      gKeyTbl[0x64] = 0;
      gKeyTbl[0x65] = 0;
      gKeyTbl[0x66] = 0;
      gKeyTbl[0x67] = 0;
      gKeyTbl[0x68] = 0;
      gKeyTbl[0x69] = 0;
      gKeyTbl[0x21] = 0;
      gKeyTbl[0x22] = 0;
      gKeyTbl[0x23] = 0;
      gKeyTbl[0x24] = 0;
      gKeyTbl[0x25] = 0;
      gKeyTbl[0x26] = 0;
      gKeyTbl[0x27] = 0;
      gKeyTbl[0x28] = 0;
    }
    byte_665E6A = (WinMsgKeyState(0x12) & 0x8000) != 0;
    if (!(gwMouseFlags_l & 0x2A)) {
      EVT_KEY(evt) = (WORD)wParam;
      EVT_MODS(evt) = (WORD)get_key_modifiers();
      if ((WORD)((DWORD)lParam >> 16) & 0x4000) {
        evt.wTag = 1;
        if (!dlg_event(&evt) && EVT_HANDLER(1))
          EVT_HANDLER(1)(&evt);
      } else {
        evt.wTag = 0;
        if (!dlg_event(&evt) && EVT_HANDLER(0))
          EVT_HANDLER(0)(&evt);
      }
    }
    if (wParam == 0x91)
      vid_paint_invalid();
    goto defproc;

  case 0x101:
  case 0x105:
    gKeyTbl[wParam] = 0;
    byte_665E6A = (WinMsgKeyState(0x12) & 0x8000) != 0;
    if (wParam == 0x2C) {
      screen_capture();
      break;
    }
    if (gwMouseFlags_l & 0x2A)
      goto defproc;
    EVT_KEY(evt) = (WORD)wParam;
    EVT_MODS(evt) = (WORD)get_key_modifiers();
    evt.wTag = 2;
    if (!dlg_event(&evt) && EVT_HANDLER(2))
      EVT_HANDLER(2)(&evt);
    goto defproc;

  case 0x112:
    if (wParam == 0xF060) {
      if (is_title_active())
        return 0;
      if (gwGameMode == 3) {
        WinMsgPost(hWnd, 0x111, 0xFFFF9C6B, 0);
        wParam = 0xF120;
        goto defproc;
      }
      SNetLeagueLogout(gLocalPlayerName);
      close_window();
      return 0;
    }
    if (wParam == 0xF100 || wParam == 0xF050)
      return 0;
    goto defproc;

  case 0x200: {
    gwMouseFlags_l |= 1;
    // Hyperion: clamped to the view's screen, 640x480 in 1.17.
    if ((WORD)lParam >= g_portView.nWidth)
      gCursorPos_g[0] = g_portView.nWidth - 1;
    else
      gCursorPos_g[0] = (WORD)lParam;
    if ((WORD)((DWORD)lParam >> 16) >= g_portView.nHeight)
      gCursorPos_g[1] = g_portView.nHeight - 1;
    else
      gCursorPos_g[1] = (WORD)((DWORD)lParam >> 16);
    break;
  }

  case 0x282:
    if (GetUserDefaultLangID() != 0x412)
      return WinMsgDefProc(hWnd, uMsg, wParam, lParam);
    return WinMsgDefProc(hWnd, uMsg, wParam, lParam);

  case 0x201:
    WinEvtButtonDown(2, 4, EVT_SLOT(4), lParam);
    break;
  case 0x202:
    WinEvtButtonUp(2, 5, EVT_SLOT(5), lParam);
    break;
  case 0x203:
    WinEvtButtonDown(2, 6, EVT_SLOT(6), lParam);
    break;
  case 0x207:
    WinEvtButtonDown(0x20, 0x0A, EVT_SLOT(0x0A), lParam);
    break;
  case 0x208:
    WinEvtButtonUp(0x20, 0x0B, EVT_SLOT(0x0B), lParam);
    break;
  case 0x209:
    WinEvtButtonDown(0x20, 0x0C, EVT_SLOT(0x0C), lParam);
    break;
  case 0x204:
    WinEvtButtonDown(8, 7, EVT_SLOT(7), lParam);
    break;
  case 0x205:
    WinEvtButtonUp(8, 8, EVT_SLOT(8), lParam);
    break;
  case 0x206:
    WinEvtButtonDown(8, 9, EVT_SLOT(9), lParam);
    break;

  case 0x20A: {
    int nDelta = (short)(wParam >> 16);
    // Hyperion: over the map in a game the wheel zooms the camera
    // (port_view.cpp); 1.17 has no use for it there. Over a layer's panels it is the layer's
    // (an add-on's list scrolls).
    if (PortOverlayWheel(nDelta))
      break;
    PortBenchViewWheel();
    if (PortViewWheel(nDelta))
      break;
    if (nDelta >= 120) {
      WinEvtMouseWheel(0x40, 0x11, EVT_SLOT(0x11), lParam);
      break;
    }
    if (nDelta <= -120) {
      WinEvtMouseWheel(0x80, 0x12, EVT_SLOT(0x12), lParam);
      break;
    }
    break;
  }

  case 0x215:
    WinEvtCaptureChanged();
    goto defproc;

  case 0x30F:
    vid_paint_invalid();
    break;

  case 0x14:
  case 0x20:
  case 0x10D:
  case 0x10E:
    break;

  case 0x311:
    if (reinterpret_cast<void *>(wParam) != hWnd)
      vid_paint_invalid();
    goto defproc;
  }

  return 1;
}

// 0x440020 close_window (42 B). Closes the flic and every window, then ends a
// running game through gameloop_init or sets gwGameMode to 2.
void close_window()
{
  CloseFlic();
  CloseAllWindows();
  if (gwGameMode == 3)
  {
    gameloop_init(2);
    return;
  }
  gwGameMode = 2;
}

// 0x440050 RunMsgDispatcher (10 B), a tail jump to RunEvtDispatcher(3).
void RunMsgDispatcher()
{
  RunEvtDispatcher(3);
}

// 0x440060 get_key_modifiers (107 B). Returns an int, as 1.17's `mov eax, esi`
// at the ret shows.
int get_key_modifiers()
{
  int nMods = 0;
  if (byte_665E68)
    nMods = 3;
  if (byte_665E69)
    nMods |= 4;
  if (byte_665E6A)
    nMods |= 8;
  if (WinMsgKeyState(0x14) & 1)      // VK_CAPITAL
    nMods |= 0x40;
  if (WinMsgKeyState(0x90) & 1)      // VK_NUMLOCK
    nMods |= 0x20;
  if (WinMsgKeyState(0x91) & 1)      // VK_SCROLL
    nMods |= 0x10;
  if (WinMsgKeyState(0x2D) & 1)      // VK_INSERT
    nMods |= 0x80;
  return nMods;
}

// 0x4400D0 SendMouseMovedEvt (94 B). Clears the moved bit and sends the
// mouse-moved event, tag 3.
void SendMouseMovedEvt()
{
  dlgevt_t evt;

  cur_update();
  gwMouseFlags_l &= ~1;
  evt.wTag = 3;
  evt.nX = static_cast<short>(gCursorPos_g[0]);
  evt.nY = static_cast<short>(gCursorPos_g[1]);
  // Hyperion: over a layer's panels that are the console's (an add-on's windows)
  // the cursor moves and nothing under them is pointed at; a drag box goes on over them
  if (!gbSelectDragging && PortCursorOverLayer(evt.nX, evt.nY)) {
    sgbMouseUpdated_l = 1;
    return;
  }
  // Hyperion: the HUD's panels are the console (port_hudinput.cpp)
  if (PortHudMouse(&evt))
    return;
  if (!dlg_event(&evt))
  {
    PFN_EVTHANDLER pfn = EVT_HANDLER(3);
    if (pfn)
      pfn(&evt);
  }
  sgbMouseUpdated_l = 1;
}

// 0x440130 GetNextMessage (130 B). ECX is the message buffer and EDX the
// blocking flag.
int GetNextMessage(intptr_t nArg0, int nArg1)
{
  if (sgbDlgDeactivating_g)
  {
    sgbDlgDeactivating_g = 0;
    return 0;
  }
  // Hyperion: not while the live channel keeps the game running (port_control.cpp), nor
  // while a LAN match is watched, which goes on as the match does (port_lanfeed.cpp).
  if (nArg1 && !sgbWindowActive_l && !PortControlKeepsRunning() && !PortLanWatchLive())
  {
    if (!gbMultiPlayerMode && gwGameMode != 2)
      return WinMsgGet(reinterpret_cast<WinMsg *>(nArg0)) != -1;
    if (WinMsgPeek(reinterpret_cast<WinMsg *>(nArg0)))
      return 1;
    Sleep(0);
    return 0;
  }
  return WinMsgPeek(reinterpret_cast<WinMsg *>(nArg0));
}

// 0x4401C0 RunEvtDispatcher (326 B). Dispatches before it translates, as 1.17
// does: TranslateMessage only posts a WM_CHAR, which the next pass picks up.
void RunEvtDispatcher(int nMode)
{
  dlgevt_t evt;
  win_msg_t msg;

  CheckAnyAsyncFailed();
  if (!nMode)
  {
    memset(gKeyTbl, 0, 0x100);
    return;
  }

  int nPeek = nMode & 2;
  while (GetNextMessage(reinterpret_cast<intptr_t>(&msg), nPeek))
  {
    // Hyperion: a key of a layer over the game that 1.17's accelerators would make a
    // command, the players' stats' F1, which is Help (port_overlay.h)
    if (PortOverlayKeyFirst(msg.message, static_cast<unsigned long long>(msg.wParam), static_cast<long long>(msg.lParam)))
      continue;
    if (!gAccelCur || !ghWnd ||
        !WinMsgTranslateAccelerator(ghWnd, gAccelCur, reinterpret_cast<const WinMsg *>(&msg)))
    {
      WinMsgDispatch(reinterpret_cast<const WinMsg *>(&msg));
      if (!sgbDlgDeactivating_g)
        WinMsgTranslate(reinterpret_cast<const WinMsg *>(&msg));
    }
  }

  // Hyperion: the throttle's idle millisecond through PortPerfIdle (port_perf.cpp),
  // which leaves it out of the numbers' parts and skips it in a benchmark run
  if (isCPUThrottleEnabled && nPeek)
    PortPerfIdle(1);
  if (gwMouseFlags_l & 1)
    SendMouseMovedEvt();

  DWORD dwNow = GetTickCount();
  if (dword_665F6C + dwNow >= 0x64)
  {
    evt.wTag = 0x0D;
    evt.nX = static_cast<short>(gCursorPos_g[0]);
    evt.nY = static_cast<short>(gCursorPos_g[1]);
    if (!dlg_event(&evt))
    {
      PFN_EVTHANDLER pfn = EVT_HANDLER(0x0D);
      if (pfn)
        pfn(&evt);
    }
    // `~dwNow + 1` rather than `-dwNow`, as 1.17 computes it.
    dword_665F6C = ~dwNow + 1;
    if (dword_665F70 + dwNow >= 0x3E8)
    {
      snd_update_streams();
      music_update();
      dword_665F70 = dword_665F6C;
    }
  }

  if (nMode & 1)
    timer_update();
}

// 0x440310 screen_capture (168 B). Saves the screen to a dated PNG and says so
// in a game.
void screen_capture()
{
  systemtime_t st;
  char szFile[0x104];
  char szMsg[0x200];

  GetLocalTime(&st);
  psnprintf(szFile, 0x104, "SCScrnShot_%02d%02d%02d_%02d%02d%02d.png",
            st.wMonth, st.wDay, st.wYear % 100, st.wHour, st.wMinute, st.wSecond);
  // Hyperion: the screen is ours, so is the capture; the oracle called
  // SDrawCaptureScreen(szFile, 0).
  if (PortScreenshot(szFile) && gwGameMode == 3)
  {
    psnprintf(szMsg, 0x200, error_get_str(0x6E), szFile);
    map_msg_chat_msg(szMsg, 0x7D0);
  }
}

// The problem-app sweep: SearchDirectoryCB 0x4403C0, ResurrectApp 0x440420,
// find_window 0x4404C0, ResurrectProblemApps 0x440530, kill_problem_apps
// 0x4405C0. 1.17 closed Microsoft Office's shortcut bar ("MOM Parent") and Find
// Fast's indexer at its start and started them again from the Start menu when
// it quit. Hyperion leaves other programs' windows alone, and
// kill_problem_apps, which WinMain calls, does nothing.
void kill_problem_apps()
{
}
