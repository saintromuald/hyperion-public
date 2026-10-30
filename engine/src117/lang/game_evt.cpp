// game_evt.cpp -- the in-game map's input event handlers. Bodies are in
// oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/game_evt.h"
#include "../h/dialog.h"     // dlgevt_t
#include "../h/rect32.h"     // the two hit rectangles game_rmouse_down_hook tests
#include "../h/Storm.h"      // PtInRect, RECT, POINT
#include "../h/placebox.h"   // set_placebox, placebox_off, placebox_gamemap_lclick
#include "../h/statbtn.h"    // placebox_is_on, target_is_on and evt_target_off
#include "../h/usel.h"       // usel_drag_start
#include "../h/cursor.h"     // cursor_get, cursor_set
#include "../h/minimap.h"    // game_hold_cursor's own declaration, cell_set_pos
#include "../h/event_Win.h"  // init_events, accel_set_single, game_reset_handlers
#include "../h/snd.h"        // snd_toggle_sound, snd_toggle_music
#include "../h/gamesnd.h"    // gamesnd_update
#include "../h/TrigAct.h"    // cur_hide, cur_show
#include "../h/victory.h"    // TryAbortCurrentTrigger
#include "../h/target.h"     // target_off
#include "../h/rclick.h"     // gamemap_rclick_default
#include "../h/CUnit.h"      // CUnit::CenterView
#include "../h/group_un.h"   // sub_463ED0
#include "../h/net_cmds.h"   // IsGamePaused, send_net_cmd_SetSpeed / _GroupUnits
#include "../h/StatLB.h"     // statlb_view_toggle
#include "../h/save_dlg.h"   // do_multi_save_dlg, do_multi_load_dlg
#include "../h/opt_dlgs.h"   // the game menu dialogs
#include "../gen/globals117.h"
#include "../port/gameplay/port_hotkeys.h"  // Hyperion: PortKeyGroup
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudConsoleCursor

// Globals this TU reaches, dual-spelled, one #ifdef per name.

// The handler slot type: the dispatchers pass the event record in ECX.
// src117/lang/usel.cpp has the same typedef.
typedef void(__fastcall *PFN_EVT_HOOK)(dlgevt_t *pEvent);

// glLastScrollPixelX / glLastScrollPixelY -- @0x543210 / 0x543214. DWORD and not
// long: they feed an unsigned division.
#ifdef OC_DLL_MODE
  #ifndef glLastScrollPixelX
  #define glLastScrollPixelX (*reinterpret_cast<DWORD *>(OC_VA(0x543210u)))
  #endif
#else
extern DWORD glLastScrollPixelX;
#endif

#ifdef OC_DLL_MODE
  #ifndef glLastScrollPixelY
  #define glLastScrollPixelY (*reinterpret_cast<DWORD *>(OC_VA(0x543214u)))
  #endif
#else
extern DWORD glLastScrollPixelY;
#endif

// sgConsoleAreaRect_g -- rect32_t @0x5033D0, handed to PtInRect as cursor.cpp
// hands sgMinimapArea_g.
#ifdef OC_DLL_MODE
  #ifndef sgConsoleAreaRect_g
  #define sgConsoleAreaRect_g (*reinterpret_cast<rect32_t *>(OC_VA(0x5033D0u)))
  #endif
#else
extern rect32_t sgConsoleAreaRect_g;
#endif

// gEventHooks -- PFN_EVT_HOOK[19] @0x665DF0, all 0x4C bytes of which init_events
// clears; 0x665DFC and the like are its elements. Spelled as
// src117/lang/usel.cpp spells it.
#ifdef OC_DLL_MODE
  #ifndef gEventHooks
  #define gEventHooks (reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x665DF0u)))
  #endif
#else
extern PFN_EVT_HOOK gEventHooks[19];
#endif

// gGameMapRect -- rect32_t @0x666430, spelled as src117/lang/cursor.cpp and
// src117/lang/usel.cpp spell it.
#ifdef OC_DLL_MODE
  #ifndef gGameMapRect
  #define gGameMapRect (*reinterpret_cast<rect32_t *>(OC_VA(0x666430u)))
  #endif
#else
extern rect32_t gGameMapRect;
#endif

// sgfnGameMapLMouseDown / sgfnGameMapRMouseDown -- @0x66728C / 0x667290, the two
// gamemap click slots.
#ifdef OC_DLL_MODE
  #ifndef sgfnGameMapLMouseDown
  #define sgfnGameMapLMouseDown (*reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x66728Cu)))
  #endif
#else
extern PFN_EVT_HOOK sgfnGameMapLMouseDown;
#endif

#ifdef OC_DLL_MODE
  #ifndef sgfnGameMapRMouseDown
  #define sgfnGameMapRMouseDown (*reinterpret_cast<PFN_EVT_HOOK *>(OC_VA(0x667290u)))
  #endif
#else
extern PFN_EVT_HOOK sgfnGameMapRMouseDown;
#endif

// gMMouseDownX_g / gMMouseDownY_g -- int @0x667294 / 0x667298.
#ifdef OC_DLL_MODE
  #ifndef gMMouseDownX_g
  #define gMMouseDownX_g (*reinterpret_cast<int *>(OC_VA(0x667294u)))
  #endif
#else
extern int gMMouseDownX_g;
#endif

#ifdef OC_DLL_MODE
  #ifndef gMMouseDownY_g
  #define gMMouseDownY_g (*reinterpret_cast<int *>(OC_VA(0x667298u)))
  #endif
#else
extern int gMMouseDownY_g;
#endif

// gwMouseFlags -- WORD @0x6D315C, tested as 16 bits.
#ifdef OC_DLL_MODE
  #ifndef gwMouseFlags
  #define gwMouseFlags (*reinterpret_cast<WORD *>(OC_VA(0x6D315Cu)))
  #endif
#else
extern WORD gwMouseFlags;
#endif

// voice_squelch -- oracle 0x4BE950, a one-byte `ret` the linker folded 80 call
// sites onto. Spelled as src117/h/net_cmds.h spells it; gEventHooks[0] casts it
// to the slot type.
void __fastcall voice_squelch(DWORD dwStormID, BOOL bSquelch);

// dlgevt_t +0x08 -- the keyboard arm, reached by cast: dialog.h leaves the slot
// `_unmapped08[4]`, since its payload depends on the event id. Both readers here
// read 16 bits of it; game_mute_key_hook reads them signed.
#define evt_vkey(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped08[0]))
#define evt_skey(p) (*reinterpret_cast<short *>(&(p)->_unmapped08[0]))

// gbTimeOutDialogActive -- BYTE @0x67500C, spelled as src117/lang/net_time.cpp
// spells it.
#ifdef OC_DLL_MODE
  #ifndef gbTimeOutDialogActive
  #define gbTimeOutDialogActive (*reinterpret_cast<BYTE *>(OC_VA(0x67500Cu)))
  #endif
#else
extern BYTE gbTimeOutDialogActive;
#endif

// word_526550 -- WORD[6] @0x526550, three saved screen positions, spelled as
// src117/lang/gamemap.cpp spells it. The recall reads each half signed.
#ifdef OC_DLL_MODE
  #ifndef word_526550
  #define word_526550 (reinterpret_cast<WORD *>(OC_VA(0x526550u)))
  #endif
#else
extern WORD word_526550[6];
#endif

// The bodies, in oracle VA order.

// Cancels the building placement box if one is up. set_placebox's third
// argument is the only one that carries information here; the other two are
// the same zeroes CUnitGUI.cpp passes.
void evt_placebox_off()
{
  if (placebox_is_on() != 0) {
    set_placebox(0, nullptr, 1);
  }
}

// Routes a printable keypress: the ten digits pick a control group, escape
// aborts a running trigger. Both arms compute `key - '0'` again rather than
// share one temporary, as 1.17 does.
void game_keychar_hook(dlgevt_t *pEvent)
{
  unsigned int nKey = evt_vkey(pEvent);

  // Hyperion: the key that selects a group as the profile binds it, 1.17's digit
  // unless it says otherwise (the hotkeys milestone K3, port_hotkeys.cpp)
  const int nGroup = PortKeyGroup(nKey);
  if (nGroup >= 0)
    game_key_down_num(static_cast<BYTE>(nGroup));

  if (nKey == 0x1B)
    TryAbortCurrentTrigger();
}

// The in-game accelerator handler: the sound toggles, then, unless a drop-wait
// dialog is up, the menu keys, then the command keys unless the game is paused.
// Case order is the oracle's physical block order.
void dialog_accel(dlgevt_t *pEvent)
{
  short nKey = evt_vkey(pEvent);

  switch (nKey) {
    case -0x639A:
      snd_toggle_music();
      break;
    case -0x6399:
      snd_toggle_sound();
      break;
  }

  if (gbTimeOutDialogActive != 0)
    return;

  switch (nKey) {
    case -0x6385: do_gamemenu_dlg(); break;
    case -0x6395: do_quit_dlg(); break;
    case -0x6393: do_quit_to_menu_dlg(); break;
    case -0x6391: do_optsmenu_dlg(); break;
    case -0x638C: do_helpmenu_dlg(); break;
    case -0x638A: do_multi_save_dlg(); break;
    case -0x6389: do_multi_load_dlg(); break;
    case -0x638B: statlb_view_toggle(); break;
  }

  if (IsGamePaused())
    return;

  switch (nKey) {
    case -0x639C:
      if (gpStatUnit != NULL)
        gpStatUnit->CenterView();
      break;
    case -0x6388:
      if (gbMultiPlayerMode == 0 && gOptions < 6)
        send_net_cmd_SetSpeed(gOptions + 1);
      break;
    case -0x6386:
      if (gbMultiPlayerMode == 0 && gOptions != 0)
        send_net_cmd_SetSpeed(gOptions - 1);
      break;
    case -0x6423: send_net_cmd_GroupUnits(0, 1, NULL, 0); break;
    case -0x6422: send_net_cmd_GroupUnits(0, 2, NULL, 0); break;
    case -0x6421: send_net_cmd_GroupUnits(0, 3, NULL, 0); break;
    case -0x6420: send_net_cmd_GroupUnits(0, 4, NULL, 0); break;
    case -0x641F: send_net_cmd_GroupUnits(0, 5, NULL, 0); break;
    case -0x641E: send_net_cmd_GroupUnits(0, 6, NULL, 0); break;
    case -0x641D: send_net_cmd_GroupUnits(0, 7, NULL, 0); break;
    case -0x641C: send_net_cmd_GroupUnits(0, 8, NULL, 0); break;
    case -0x641B: send_net_cmd_GroupUnits(0, 9, NULL, 0); break;
    case -0x641A: send_net_cmd_GroupUnits(0, 0, NULL, 0); break;
    case -0x642D: send_net_cmd_GroupUnits(2, 1, NULL, 0); break;
    case -0x642C: send_net_cmd_GroupUnits(2, 2, NULL, 0); break;
    case -0x642B: send_net_cmd_GroupUnits(2, 3, NULL, 0); break;
    case -0x642A: send_net_cmd_GroupUnits(2, 4, NULL, 0); break;
    case -0x6429: send_net_cmd_GroupUnits(2, 5, NULL, 0); break;
    case -0x6428: send_net_cmd_GroupUnits(2, 6, NULL, 0); break;
    case -0x6427: send_net_cmd_GroupUnits(2, 7, NULL, 0); break;
    case -0x6426: send_net_cmd_GroupUnits(2, 8, NULL, 0); break;
    case -0x6425: send_net_cmd_GroupUnits(2, 9, NULL, 0); break;
    case -0x6424: send_net_cmd_GroupUnits(2, 0, NULL, 0); break;
    case -0x640F: sub_463ED0(1); break;
    case -0x640E: sub_463ED0(2); break;
    case -0x640D: sub_463ED0(3); break;
    case -0x640C: sub_463ED0(4); break;
    case -0x640B: sub_463ED0(5); break;
    case -0x640A: sub_463ED0(6); break;
    case -0x6409: sub_463ED0(7); break;
    case -0x6408: sub_463ED0(8); break;
    case -0x6407: sub_463ED0(9); break;
    case -0x6406: sub_463ED0(0); break;
    case -0x6384:
      word_526550[0] = static_cast<WORD>(glScrollPixelX);
      word_526550[1] = static_cast<WORD>(glScrollPixelY);
      break;
    case -0x6383:
      word_526550[2] = static_cast<WORD>(glScrollPixelX);
      word_526550[3] = static_cast<WORD>(glScrollPixelY);
      break;
    case -0x6382:
      word_526550[4] = static_cast<WORD>(glScrollPixelX);
      word_526550[5] = static_cast<WORD>(glScrollPixelY);
      break;
    case -0x6381:
      cell_set_pos(static_cast<short>(word_526550[0]), static_cast<short>(word_526550[1]));
      break;
    case -0x6380:
      cell_set_pos(static_cast<short>(word_526550[2]), static_cast<short>(word_526550[3]));
      break;
    case -0x637F:
      cell_set_pos(static_cast<short>(word_526550[4]), static_cast<short>(word_526550[5]));
      break;
  }
}

// Drops whatever modal map interaction is up and reinstalls the default
// handlers; the tail call to 0x4BE950 is taken only when the caller asks. It is
// the nullary alias, declared in block scope so that game_reset_handlers'
// &voice_squelch stays unambiguous.
void game_event_reset(int nSquelchVoice)
{
  void __fastcall voice_squelch();

  if (placebox_is_on())
    placebox_off();

  if (target_is_on())
    target_off();

  game_reset_handlers();

  if (nSquelchVoice)
    voice_squelch();
}

// Pins the cursor for a drag: the move, up and second-button slots all become
// the reset, and the moved slot is cleared. With no mouse present there is
// nothing to pin and the whole thing is a reset.
void __fastcall game_hold_cursor(WORD wCursor)
{
  if (gwMouseFlags != 0) {
    gEventHooks[8]  = reinterpret_cast<PFN_EVT_HOOK>(&game_reset_handlers);
    gEventHooks[11] = reinterpret_cast<PFN_EVT_HOOK>(&game_reset_handlers);
    gEventHooks[5]  = reinterpret_cast<PFN_EVT_HOOK>(&game_reset_handlers);
    gEventHooks[3]  = nullptr;
    cursor_set(wCursor);
  } else {
    game_reset_handlers();
  }
}

// Forwards a left click on the map to whichever handler the current mode
// installed, unless the cursor is one of the non-map ones.
void game_lmouse_down_hook(dlgevt_t *pEvent)
{
  if (cursor_get() < 11u)
    sgfnGameMapLMouseDown(pEvent);
}

// Starts a middle-button map drag. The two anchors are the click position
// biased by how far the map has already scrolled, in the same hundredths the
// minimap uses; game_mmouse_moved_hook reverses the arithmetic.
void game_mmouse_down_hook(dlgevt_t *pEvent)
{
  // Hyperion: the scrollable extent is the map less the view, 640 by 400
  // in 1.17 (port_view.cpp), and at least 1: a map no wider than a wide view
  // does not scroll across.
  const int nExtentX = gwMapPixWidth - g_portView.nWidth;
  const int nExtentY = gwMapPixHeight - g_portView.nViewRows;
  gMMouseDownX_g = pEvent->nX
                 - glLastScrollPixelX * 100 / (nExtentX > 0 ? nExtentX : 1) + 0x32;
  gMMouseDownY_g = pEvent->nY
                 - glLastScrollPixelY * 100 / (nExtentY > 0 ? nExtentY : 1) + 0x32;
  cur_hide();
  gEventHooks[3] = game_mmouse_moved_hook;
}

// The high bound is tested twice: 1.17's code tests it on both paths. A macro,
// so that no helper function is emitted beside the body.
#define OC_CLAMP(v, lo, hi) \
  ((v) > (hi) || (v) >= (lo) ? ((v) > (hi) ? (hi) : (v)) : (lo))

// Scrolls the map while the middle button is held. The pointer travel is
// clamped to 0..100 and then scaled back onto the map's scrollable extent. Both
// differences are taken first, Y then X, with the +0x32 bias inside the clamp.
void game_mmouse_moved_hook(dlgevt_t *pEvent)
{
  int nY = gCursorPos_g[1] - gMMouseDownY_g;
  int nX = gCursorPos_g[0] - gMMouseDownX_g;
  nX = OC_CLAMP(nX + 0x32, 0, 100);
  nY = OC_CLAMP(nY + 0x32, 0, 100);

  cell_set_pos((gwMapPixWidth - g_portView.nWidth) * nX / 100,
               (gwMapPixHeight - g_portView.nViewRows) * nY / 100);  // Hyperion: 0x280, 0x190
}

// Ends the middle-button drag.
void game_mmouse_up_hook(dlgevt_t *pEvent)
{
  cur_show();
  gEventHooks[3] = nullptr;
}

// Reinstalls the whole in-game handler set, then picks the pair of gamemap
// click handlers the current mode wants. The store order below is the
// oracle's, which is not the index order.
void game_reset_handlers()
{
  init_events();

  gEventHooks[0]  = reinterpret_cast<PFN_EVT_HOOK>(&voice_squelch);
  gEventHooks[4]  = game_lmouse_down_hook;
  gEventHooks[6]  = game_lmouse_down_hook;
  gEventHooks[10] = game_mmouse_down_hook;
  gEventHooks[11] = game_mmouse_up_hook;
  gEventHooks[7]  = game_rmouse_down_hook;
  gEventHooks[9]  = game_rmouse_down_hook;
  gEventHooks[3]  = nullptr;
  gEventHooks[13] = game_timer;
  gEventHooks[15] = game_keychar_hook;
  gEventHooks[17] = nullptr;
  gEventHooks[18] = nullptr;

  accel_set_single();

  // Three of the four bodies below are declared with a narrower parameter
  // list than the slot's, so they cast.
  if (placebox_is_on()) {
    sgfnGameMapLMouseDown = reinterpret_cast<PFN_EVT_HOOK>(&placebox_gamemap_lclick);
    sgfnGameMapRMouseDown = reinterpret_cast<PFN_EVT_HOOK>(&evt_placebox_off);
    return;
  }

  if (target_is_on()) {
    sgfnGameMapLMouseDown = gamemap_lclick_target;
    sgfnGameMapRMouseDown = reinterpret_cast<PFN_EVT_HOOK>(&evt_target_off);
    return;
  }

  sgfnGameMapLMouseDown = usel_drag_start;
  sgfnGameMapRMouseDown = gamemap_rclick_default;
}

// Forwards a right click on the map. Only a click inside the play area that is
// not over the console reaches the mode's own handler; everything else is the
// generic screen right-click.
void game_rmouse_down_hook(dlgevt_t *pEvent)
{
  if (cursor_get() < 11u) {
    // Hyperion: 1.17's card where a HUD design shows it, through its slot; 1.17's
    // place for it is open map beside a column or above a lower HUD
    LONG anAt[2];
    PortHudConsoleCursor(anAt);
    if (PtInRect(reinterpret_cast<const RECT *>(&sgConsoleAreaRect_g),
                 *reinterpret_cast<const POINT *>(anAt))) {
      screen_rclick(pEvent);
      return;
    }
    if (PtInRect(reinterpret_cast<const RECT *>(&gGameMapRect),
                 *reinterpret_cast<const POINT *>(gCursorPos_g)))
      sgfnGameMapRMouseDown(pEvent);
    else
      screen_rclick(pEvent);
  }
}

// The per-frame tick the event table drives.
void game_timer(dlgevt_t *pEvent)
{
  gamesnd_update();
}

// The accelerator handler installed while sound options are open: two keys,
// nothing else.
void game_mute_key_hook(dlgevt_t *pEvent)
{
  int nKey = evt_skey(pEvent);

  switch (nKey) {
    case -0x639A:
      snd_toggle_music();
      break;
    case -0x6399:
      snd_toggle_sound();
      break;
  }
}
