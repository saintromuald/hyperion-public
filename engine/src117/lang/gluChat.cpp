// gluChat.cpp -- the multiplayer chat room and lobby screen: its dialog fcn and activate,
// the message listbox and send-edit controls, the kick confirmation and the chat-received
// handler. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here. A callee
// defined in this file can be spliced into a caller in this file by /Ob2; the oracle is
// WPO-off, so a cross-TU call cannot be, and the opposite case is a body whose size is
// exactly caller plus callee, which needs its callee here. The lever when a callee must
// live here anyway is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/gluChat.h"
#include "../h/dialog.h"       // ctrl_t, dlgevt_t, lbox_t, PFN_CTRL_EVT
#include "../h/gluChatSlot.h"  // sub_451780, sub_451AD0, sub_4520C0
#include "../h/dlg_ctrl.h"     // ctrl_find_ndx / ctrl_hide / ctrl_send_msg / ctrl_text
#include "../h/dlg_dlg.h"      // dlg_load, dlg_run_modal, dlg_set_font
#include "../h/dlg_lbox.h"     // lbox_set_selection / lbox_set_recalc / lbox_remove_all
#include "../h/glues.h"        // glues_get_str / glues_clear_all / glues_transition_in / alloc_load_bmp
#include "../h/gluPopup.h"     // do_pok_dlg, do_pok_cancel_dlg
#include "../h/save_dlg.h"     // edit_ctrl_edit, edit_set_recalc, lbox_ctrl_list
#include "../h/net_glue.h"     // IsGameCreator, NetworkMode
#include "../h/net_player.h"   // net_player_get_name
#include "../h/event_Win.h"    // init_lang_imm
#include "../h/replay.h"       // map_free
#include "../h/snd.h"          // load_game_data
#include "../h/victory.h"      // psnprintf
#include "../h/minimap.h"       // minimap_deactivate, do_minimappreview_dlg, timer_create
#include "../h/game.h"          // free_game_minimap, game_minimap_init
#include "../h/net_rand.h"      // set_rand_enabled
#include "../h/fd_sessn.h"      // IsSessionComplete
#include "../h/gametype.h"      // gametype_find_template_name / _param_name, IsWGTGame
#include "../h/gluBNCreate.h"   // gametype_find_template, gametype_get_subtype_name
#include "../h/gluRdy.h"        // glues_transition_out
#include "../h/uiMulti.h"       // StartNetGame, sub_4BAF60
#include "../h/textbox.h"       // SNetGetReplyName
#include "../h/dlg_draw.h"      // font_strlen
#include "../h/gds_font.h"      // string_width
#include "../h/Storm.h"        // SStrCopy
#include "../gen/globals117.h"
#include "../port/lan/port_lanlobby.h"  // Hyperion: PortChatPress, PortLanLobbyTick, PortLanChatLeft, PortLanChatWhy
#include "../port/data/port_chatfilter.h"  // Hyperion: PortChatClean

// The globals this TU reaches, spelled both ways with one guard per name: a `#define`
// over OC_VA under OC_DLL_MODE, a plain `extern` otherwise. Where the bytes do not give a
// global's role, its name is its address (`dword_667C1C`). gCtrlEvtFcnTbl @0x4F10F8,
// g_LocalStormID @0x508D58, glGluesMode @0x66A274 and gPlayback @0x67CC90 are not
// respelled here; they come from ../gen/globals117.h.

// 0x6CE6DC gnDlgReturnVal, long. sub_450980 stores its second argument here
// (`mov dword ptr [0x6CE6DC], esi`). Four other TUs spell it locally too.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x667C04 -- the live chat-room dialog. do_gluChat_dlg stores dlg_load's
// result here; sub_450980 writes `[eax+4Ah]` through it (ctrl_t::pfnDlgProc) and
// hands it to ctrl_send_msg; sub_451770 and sub_4517A0 load it into ECX for
// sub_451AD0 / ctrl_find_ndx. A ctrl_t * on every one of those readings.
#ifdef OC_DLL_MODE
#define sgpChatDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x667C04u)))
#else
extern ctrl_t *sgpChatDlg;
#endif

// 0x667C0C -- read once in the whole slice, as `cmp dword ptr [0x667C0C], 0h`
// in sub_450980, and written nowhere in it. The width is proven and the role is
// not, so the name is the address.
#ifdef OC_DLL_MODE
#define dword_667C0C (*reinterpret_cast<DWORD *>(OC_VA(0x667C0Cu)))
#else
extern DWORD dword_667C0C;
#endif

// 0x667C10 -- the message-to-send edit control. ctrl_edit_msgtosend_ctrl_fcn
// stores its own pCtrl here (`mov dword ptr [0x667C10], esi`) on the control's
// init event, one instruction before handing the same pointer to
// edit_set_recalc, whose ECX is a ctrl_t *.
#ifdef OC_DLL_MODE
#define sgpChatEditCtrl (*reinterpret_cast<ctrl_t **>(OC_VA(0x667C10u)))
#else
extern ctrl_t *sgpChatEditCtrl;
#endif

// 0x667C14 -- the message listbox. ctrl_listbox_msgs_ctrl_fcn stores its own
// pCtrl here on init and loads it straight into ECX for lbox_remove_all, whose
// ECX is an lbox_t *. File-static in the proof arm: gluChat_write_msg loads it above
// a store through the line buffer, which MSVC does only for a global whose
// address cannot leave the TU.
#ifdef OC_DLL_MODE
#define sgpChatMsgList (*reinterpret_cast<lbox_t **>(OC_VA(0x667C14u)))
#else
extern lbox_t *sgpChatMsgList;
#endif

// 0x667C18 -- set to 1 when IsGameCreator succeeds and to 0 when it does not
// (gluChat_dlg_activate), and compared against 0 by ctrl_btn_ok_ctrl to decide
// whether the OK button's command is swallowed. Stored and compared as a whole
// dword.
#ifdef OC_DLL_MODE
#define sgnChatIsCreator (*reinterpret_cast<DWORD *>(OC_VA(0x667C18u)))
#else
extern DWORD sgnChatIsCreator;
#endif

// 0x667C1C -- sub_451790 sets it to 1, do_gluChat_dlg clears it before the
// modal. Width proven (`mov dword ptr [..], 1h`), role not derived.
#ifdef OC_DLL_MODE
#define dword_667C1C (*reinterpret_cast<DWORD *>(OC_VA(0x667C1Cu)))
#else
extern DWORD dword_667C1C;
#endif

// 0x667C20 -- on_chrm_kicked sets it to 1, do_gluChat_dlg clears it. Paired
// with 0x667C24 below: the flag that says a kick reason has been recorded.
#ifdef OC_DLL_MODE
#define dword_667C20 (*reinterpret_cast<DWORD *>(OC_VA(0x667C20u)))
#else
extern DWORD dword_667C20;
#endif

// 0x667C24 -- the kick reason, written by on_chrm_kicked as one of ten dword constants in
// 0x74..0x7D. Those are glues_get_str string ids -- ConfirmKick in this same TU calls it
// with the neighbouring 0x7E -- so the name is the role rather than the address.
#ifdef OC_DLL_MODE
#define sgnChatKickStrId (*reinterpret_cast<DWORD *>(OC_VA(0x667C24u)))
#else
extern DWORD sgnChatKickStrId;
#endif

// 0x667C28 -- sub_451780 sets it to 1, do_gluChat_dlg clears it. Same shape as
// 0x667C1C; role not derived.
#ifdef OC_DLL_MODE
#define dword_667C28 (*reinterpret_cast<DWORD *>(OC_VA(0x667C28u)))
#else
extern DWORD dword_667C28;
#endif

// 0x665F58 / 0x665F5C -- ctrl_edit_msgtosend_char compares the first against 1
// and, only then, stores 1 into the second. Both whole dwords; neither role is
// derived from this body, and no other row in the slice touches either.
#ifdef OC_DLL_MODE
#define dword_665F58 (*reinterpret_cast<DWORD *>(OC_VA(0x665F58u)))
#else
extern DWORD dword_665F58;
#endif

#ifdef OC_DLL_MODE
#define dword_665F5C (*reinterpret_cast<DWORD *>(OC_VA(0x665F5Cu)))
#else
extern DWORD dword_665F5C;
#endif

// 0x5039F8 -- do_gluChat_dlg sets it to 1 before loading the dialog and never
// reads it. In .data, whole dword, role not derived.
#ifdef OC_DLL_MODE
#define dword_5039F8 (*reinterpret_cast<DWORD *>(OC_VA(0x5039F8u)))
#else
extern DWORD dword_5039F8;
#endif

// 0x503B50 -- the chat screen's transition-control table, handed to glues_transition_in
// in EDX (`mov edx, 503B50h`), the same slot glues.cpp reads as an array of 4-byte
// records (a WORD index and a signed WORD edge). Only its address is taken here, so it is
// spelled as a byte array; the bytes at that VA are five records and a zero terminator,
// 24 in all.
#ifdef OC_DLL_MODE
#define sgChatTransCtrls (reinterpret_cast<char *>(OC_VA(0x503B50u)))
#else
extern char sgChatTransCtrls[24];
#endif

// 0x666564 -- set by the map loader once a map file is chosen; sub_4505F0
// clears it and sub_450640 refuses to build the preview while it is 0. The name
// and spelling are map_load.cpp's.
#ifdef OC_DLL_MODE
#define sgbIsMapFilenameSet (*reinterpret_cast<DWORD *>(OC_VA(0x666564u)))
#else
extern DWORD sgbIsMapFilenameSet;
#endif

// 0x667C2C / 0x667C30 -- the minimap-preview hover flags. The hover handler
// clears the first and sets the second; gluChat_mouse_moved needs both. Whole
// dwords; roles not derived beyond that, so the names are the sheet's.
#ifdef OC_DLL_MODE
#define dword_667C2C (*reinterpret_cast<DWORD *>(OC_VA(0x667C2Cu)))
#else
extern DWORD dword_667C2C;
#endif

#ifdef OC_DLL_MODE
#define dword_667C30 (*reinterpret_cast<DWORD *>(OC_VA(0x667C30u)))
#else
extern DWORD dword_667C30;
#endif

// 0x667C34 -- a 0x18-byte buffer SNetGetGameInfo(2, ...) fills; gluChat_update_password
// tests its first byte and hands it to glue_set_text.
#ifdef OC_DLL_MODE
#define byte_667C34 (reinterpret_cast<char *>(OC_VA(0x667C34u)))
#else
extern char byte_667C34[0x18] OC_UNALIGNED;
#endif

// 0x668090 -- the countdown line updateChrmCountdown formats (0x100 bytes).
#ifdef OC_DLL_MODE
#define byte_668090 (reinterpret_cast<char *>(OC_VA(0x668090u)))
#else
extern char byte_668090[0x100];
#endif

// 0x672960 -- net_glue.cpp's session pointer. sub_450640 hands its value to
// IsSessionComplete, whose parameter is a TFD_SESSION *.
#ifdef OC_DLL_MODE
#define dword_672960 (*reinterpret_cast<TFD_SESSION **>(OC_VA(0x672960u)))
#else
extern TFD_SESSION *dword_672960;
#endif

// gGameHeader fields gluChat_update_game_info and gluChat_dlg_fcn read, spelled per address
// because the embedded record's layout (uiMulti.cpp's GameData) has no header.
// 0x6728D4 and 0x67291D are the game and map name strings, 0x6728EC the
// record's first dword, 0x6728F5 the slot count, 0x6728F6 the game speed.
#ifdef OC_DLL_MODE
#define byte_6728D4 (reinterpret_cast<char *>(OC_VA(0x6728D4u)))
#else
extern char byte_6728D4[0x18] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define dword_6728EC (*reinterpret_cast<DWORD *>(OC_VA(0x6728ECu)))
#else
extern DWORD dword_6728EC;
#endif

#ifdef OC_DLL_MODE
#define word_6728F0 (*reinterpret_cast<WORD *>(OC_VA(0x6728F0u)))
#else
extern WORD word_6728F0;
#endif

#ifdef OC_DLL_MODE
#define word_6728F2 (*reinterpret_cast<WORD *>(OC_VA(0x6728F2u)))
#else
extern WORD word_6728F2;
#endif

#ifdef OC_DLL_MODE
#define byte_6728F5 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F5u)))
#else
extern BYTE byte_6728F5;
#endif

#ifdef OC_DLL_MODE
#define byte_6728F6 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F6u)))
#else
extern BYTE byte_6728F6;
#endif

#ifdef OC_DLL_MODE
#define byte_6728F8 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F8u)))
#else
extern BYTE byte_6728F8;
#endif

#ifdef OC_DLL_MODE
#define byte_6728F9 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F9u)))
#else
extern BYTE byte_6728F9;
#endif

#ifdef OC_DLL_MODE
#define word_6728FA (*reinterpret_cast<WORD *>(OC_VA(0x6728FAu)))
#else
extern WORD word_6728FA;
#endif

#ifdef OC_DLL_MODE
#define byte_672903 (*reinterpret_cast<BYTE *>(OC_VA(0x672903u)))
#else
extern BYTE byte_672903;
#endif

#ifdef OC_DLL_MODE
#define byte_67291D (reinterpret_cast<char *>(OC_VA(0x67291Du)))
#else
extern char byte_67291D[0x20] OC_UNALIGNED;
#endif

// 0x667C4C / 0x667C70 / 0x667C90 -- gluChat_update_game_info's three text buffers: the
// formatted player count (0x23), the parameter name (0x20) and the subtype
// line (0x400), sized by the capacities the body passes.
#ifdef OC_DLL_MODE
#define byte_667C4C (reinterpret_cast<char *>(OC_VA(0x667C4Cu)))
#else
extern char byte_667C4C[0x23] OC_UNALIGNED;
#endif

#ifdef OC_DLL_MODE
#define byte_667C70 (reinterpret_cast<char *>(OC_VA(0x667C70u)))
#else
extern char byte_667C70[0x20];
#endif

#ifdef OC_DLL_MODE
#define byte_667C90 (reinterpret_cast<char *>(OC_VA(0x667C90u)))
#else
extern char byte_667C90[0x400];
#endif

// 0x4ED688 -- the game-speed string table in .rdata: seven {string id, speed}
// word pairs, 0x55..0x5B against speeds 0..6. 0x5039FC holds its count (7),
// read with `movsx`.
struct gamespeedstr_t {
  /*0x00*/ short nStrId;
  /*0x02*/ short nSpeed;
};
static_assert(offsetof(gamespeedstr_t, nStrId) == 0x00, "movsx word ptr [ecx*4+4ED688h]");
static_assert(offsetof(gamespeedstr_t, nSpeed) == 0x02, "movsx word ptr [ecx*4+4ED68Ah]");
static_assert(sizeof(gamespeedstr_t) == 4, "scaled by 4");

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgGameSpeedStrs_g (reinterpret_cast<const gamespeedstr_t *>(OC_VA(0x4ED688u)))
#else
extern const gamespeedstr_t sgGameSpeedStrs_g[7];
#endif

#ifdef OC_DLL_MODE
#define word_5039FC (*reinterpret_cast<short *>(OC_VA(0x5039FCu)))
#else
extern short word_5039FC;
#endif

// 0x503A00 -- the chat screen's per-control event table, handed to
// glue_init_screen_dlg with its byte size 0x150.
#ifdef OC_DLL_MODE
#define sChatEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503A00u)))
#else
extern PFN_CTRL_EVT sChatEvtFcnTbl[0x150 / 4] OC_UNALIGNED;
#endif

// 0x6726AC -- the name a /reply answers (0x19 bytes), filled here by
// SNetGetReplyName. Spelled as textbox.cpp spells it.
#ifdef OC_DLL_MODE
#define gReplyPlayerName_g (reinterpret_cast<char *>(OC_VA(0x6726ACu)))
#else
extern char gReplyPlayerName_g[];
#endif

// 0x6D3036 -- the font's widest glyph (gds_font.cpp's name). gluChat_write_msg
// subtracts it from the list width with `movzx`, so a BYTE.
#ifdef OC_DLL_MODE
#define gbFontMaxWdt (*reinterpret_cast<BYTE *>(OC_VA(0x6D3036u)))
#else
extern BYTE gbFontMaxWdt;
#endif

// The bodies, in oracle VA order.

// 0x4505F0, 77 B. Leave the minimap preview, or the in-game minimap when no
// preview is up.
void sub_4505F0(intptr_t nArg0)
{
  if (!sgbIsMinimapPreview_g) {
    if (sgpMinimapDlg != NULL)
      minimap_deactivate();
    return;
  }

  if (dword_667C30 == 1) {
    gluChat_mouse_moved(nArg0, 0, 1);
    dword_667C30 = 0;
  }
  free_game_minimap();
  sgbIsMinimapPreview_g = 0;
  sgbIsMapFilenameSet = 0;
}

// 0x450640, 100 B. The preview timer's callback: build the preview minimap once
// a map is chosen, with the game's random stream switched off around it, then
// drop the timer.
void sub_450640(intptr_t nArg0)
{
  if (sgbIsMinimapPreview_g != 1 && sgbIsMapFilenameSet != 0 &&
      g_LocalNationID < 8 &&
      (dword_672960 == NULL || IsSessionComplete(dword_672960))) {
    int nRand;

    sgbIsMinimapPreview_g = 1;
    nRand = set_rand_enabled(1);
    game_minimap_init();
    set_rand_enabled(nRand);
    timer_remove_node(reinterpret_cast<ctrl_t *>(nArg0), 1);
  }
}

// 0x4506B0, 136 B. Record why the chat room kicked us. A dense jump table over
// `bReason - 1` in 0..8, everything else defaulting, and the arm order matters: the
// nine-entry dword table sits in the COMDAT tail, where its targets are compared against
// the oracle's. The stored values are not monotonic (case 5 -> 0x7A, case 6 -> 0x7D,
// case 7 -> 0x79). The `dword_667C20 = 1` store is written first in source; MSVC slides
// it between the `dec` and the `cmp`.
void on_chrm_kicked(BYTE bReason)
{
  dword_667C20 = 1;
  switch (bReason) {
    case 1: sgnChatKickStrId = 0x75; break;
    case 2: sgnChatKickStrId = 0x76; break;
    case 3: sgnChatKickStrId = 0x77; break;
    case 4: sgnChatKickStrId = 0x78; break;
    case 5: sgnChatKickStrId = 0x7A; break;
    case 6: sgnChatKickStrId = 0x7D; break;
    case 7: sgnChatKickStrId = 0x79; break;
    case 8: sgnChatKickStrId = 0x7B; break;
    case 9: sgnChatKickStrId = 0x7C; break;
    default: sgnChatKickStrId = 0x74; break;
  }
}

// 0x450760, 72 B. Run the minimap preview dialog, then arm the one-second timer
// whose callback builds the preview.
void gluChat_hover_minimap_preview(intptr_t nArg0)
{
  dword_667C2C = 0;
  dword_667C30 = 1;
  sgbIsMinimapPreview_g = 1;
  do_minimappreview_dlg();
  sgbIsMinimapPreview_g = 0;
  timer_create(reinterpret_cast<ctrl_t *>(nArg0), 1, 1000,
               reinterpret_cast<PFN_TIMER_FN *>(sub_450640));
}

// 0x4507B0, 315 B. Swap the map-info controls against the preview minimap.
void gluChat_mouse_moved(intptr_t nArg0, int nArg1, int nArg2)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  char szInfo[0x18];

  if (sgbIsMinimapPreview_g == 0 || dword_667C2C == 0 || dword_667C30 == 0)
    return;

  SNetGetGameInfo(2, szInfo, 0x18, NULL);
  if (szInfo[0] != 0) {
    ctrl_showhide(ctrl_find_ndx(pDlg, 0x0C), nArg1 == 0);
    ctrl_showhide(ctrl_find_ndx(pDlg, 0x10), nArg1 == 0);
  }
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x0D), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x12), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x0E), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x13), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x0F), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(pDlg, 0x14), nArg1 == 0);
  ctrl_showhide(ctrl_find_ndx(sgpMinimapDlg, 1), nArg1);
  ctrl_showhide(ctrl_find_ndx(sgpChatDlg, 6), nArg2);
}

// 0x4508F0, 24 B. The map-info text control's event handler: it sets one flag bit on its
// own control for one (wTag, dwId) pair, then tail-jumps into the generic text handler
// with both registers untouched. The `or` is the 7-byte imm32 form, since 0x40000000 does
// not fit an imm8.
BOOL ctrl_mapinfo_text(void *pArg0, void *pArg1)
{
  ctrl_t *pCtrl = static_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = static_cast<dlgevt_t *>(pArg1);

  if (pEvent->wTag == 0x0E && pEvent->dwId == 0x0A)
    pCtrl->dwFlags |= 0x40000000;

  return ctrl_text(pCtrl, pEvent);
}

// 0x450910, 101 B. The chat room's activate. The zero arm is physically first: the `jnz`
// off IsGameCreator's result jumps forward to the `= 1` store, so the two hides and the
// `= 0` store are the fallthrough, and `if (!IsGameCreator(...))` is what reproduces that.
// glues_transition_in's third parameter is declared `void *` while the oracle pushes the
// literal 5, so the cast below is the declaration's shape, not a pointer.
void gluChat_dlg_activate(intptr_t nArg0)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  if (!IsGameCreator(NULL, 0)) {
    ctrl_hide(ctrl_find_ndx(pDlg, 5));
    ctrl_hide(ctrl_find_ndx(pDlg, 7));
    sgnChatIsCreator = 0;
  } else {
    sgnChatIsCreator = 1;
  }

  gluChat_update_game_info(reinterpret_cast<intptr_t>(pDlg));
  glues_transition_in(reinterpret_cast<intptr_t>(pDlg), sgChatTransCtrls,
                      5, 2, 0);
}

// 0x450980, 73 B. Close the chat room down with a reason and a modal result. The dialog's
// own +0x4A callback is cleared first, then either a popup is raised with a string id or
// the live popup is torn down, then the modal result is published and, conditionally, the
// dialog is told to close. The gnDlgReturnVal store is written first here because the
// oracle reaches its `ret` with it done on both paths; MSVC schedules the
// `cmp dword ptr [0x667C0C], 0h` ahead of it.
void sub_450980(int nArg0, int nArg1)
{
  sgpChatDlg->pfnDlgProc = NULL;

  // Hyperion: an unseen room's match says why on the home screen
  if (nArg0) {
    if (!PortLanChatWhy(glues_get_str(nArg0)))
      do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(nArg0)));
  } else {
    deactivate_popup();
  }

  gnDlgReturnVal = nArg1;
  if (dword_667C0C == 0)
    ctrl_send_msg(sgpChatDlg, 2, 0);
}

// 0x4509D0, 348 B. The chat room's command handler: 7 starts the game after
// three refusals, 9 sends the typed line, 8 cancels and 0x22D leaves. Case 8
// falls into the 0x22D arm, and only the first two refusals of case 7 are
// skipped in playback; both are read off the oracle's branch targets.
BOOL gluChat_cmd(intptr_t nArg0, int nArg1)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_t *pEdit;

  switch (nArg1) {
    case 7:
      if (gPlayback == 0) {
        if (CountSides() < 2) {
          do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x72)));
          return TRUE;
        }
        if (IsWGTGame() && sub_451940(1) < 2) {
          do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0xBA)));
          return TRUE;
        }
      }
      if (dword_672960 != NULL && !IsSessionComplete(dword_672960)) {
        do_pok_dlg(reinterpret_cast<intptr_t>(glues_get_str(0x73)));
        return TRUE;
      }
      StartNetGame();
      ctrl_gray(ctrl_find_ndx(pDlg, 7));
      gluChat_mouse_moved(nArg0, 0, 0);
      dword_667C30 = 0;
      return TRUE;
    case 9:
      if (*static_cast<char *>(sgpChatEditCtrl->pData) != 0)
        sub_474FB0(reinterpret_cast<intptr_t>(sgpChatEditCtrl->pData));
      pEdit = sgpChatEditCtrl;
      *static_cast<char *>(pEdit->pData) = 0;
      *reinterpret_cast<dlg_byte_t *>(&pEdit->pFocus) = 0;
      ctrl_set_update(pEdit);
      break;
    case 8:
      ctrl_gray(ctrl_find_ndx(pDlg, 8));
      sub_474E60();
      // fall through
    case 0x22D:
      sub_4BAF60();
      gluChat_mouse_moved(nArg0, 0, 1);
      dword_667C30 = 0;
      // fall through
    default:
      sgpChatDlg->pfnDlgProc = NULL;
      return glues_transition_out(pDlg);
  }

  return TRUE;
}

// Hyperion: a button of the chat room pressed for its player, 7 Start Game and 8 Cancel,
// by a LAN room's match (port_lanlobby.cpp).
void PortChatPress(int nCmd)
{
  if (sgpChatDlg != NULL && sgpChatDlg->pfnDlgProc != NULL)
    gluChat_cmd(reinterpret_cast<intptr_t>(sgpChatDlg), nCmd);
}

// 0x450B30, 498 B. The chat room's dialog procedure.
//
// A wTag of 3 (mouse move) falls through into the 15 (key) arm, as the
// oracle's layout has it.
BOOL gluChat_dlg_fcn(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);
  ctrl_t *pCtrl;

  switch (pEvent->wTag) {
    case 0x0E:
      switch (pEvent->dwId) {
        case 0:
          gluChat_dlg_activate(nArg0);
          glue_screen_activate(pDlg);
          ctrl_dlg(pDlg, pEvent);
          gluChat_start_poll(nArg0);
          return TRUE;
        case 1:
          pDlg->pfnDlgProc = NULL;
          sub_4518D0(nArg0);
          sub_474C30();
          dword_667C0C = 1;
          sub_452360(nArg0);
          sub_4505F0(nArg0);
          break;
        case 2:
          return gluChat_cmd(nArg0, gnDlgReturnVal);
        case 0x0A:
          glue_init_screen_dlg(pDlg, sChatEvtFcnTbl, 0x150, 0);
          dword_667C0C = 0;
          if (gPlayback == 0 && dword_6728EC == 0 && !IsScenarioGame(NULL))
            gluChat_hover_minimap_preview(nArg0);
          else
            ctrl_showhide(ctrl_find_ndx(pDlg, 6), 0);
          break;
        case 0x405:
          gluChat_update_password(nArg0);
          sub_451AB0();
          ctrl_hide(ctrl_find_ndx(pDlg, 0x17));
          ctrl_hide(ctrl_find_ndx(pDlg, 0x18));
          dword_667C2C = 1;
          return TRUE;
      }
      break;
    case 0x11:
      pCtrl = ctrl_find_ndx(pDlg, 0x0B);
      ctrl_send_msg(*reinterpret_cast<ctrl_t **>(pCtrl->_unmapped36), 0x0F, 0);
      return TRUE;
    case 0x12:
      pCtrl = ctrl_find_ndx(pDlg, 0x0B);
      ctrl_send_msg(*reinterpret_cast<ctrl_t **>(pCtrl->_unmapped36), 0x10, 0);
      return TRUE;
    case 3:
      pCtrl = ctrl_find_ndx(pDlg, 6);
      gluChat_mouse_moved(nArg0,
                          pEvent->nX >= pCtrl->rc.left && pEvent->nX <= pCtrl->rc.right &&
                          pEvent->nY >= pCtrl->rc.top && pEvent->nY <= pCtrl->rc.bottom,
                          1);
      // fall through
    case 0x0F:
      if (*reinterpret_cast<const unsigned short *>(pEvent->_unmapped08) == 0x2F)
        SNetGetReplyName(gReplyPlayerName_g, 0x19);
      break;
  }

  return ctrl_dlg(pDlg, pEvent);
}

// 0x450D50, 40 B. The OK button's event handler: it swallows its own command event when
// this client is not the game creator, and otherwise falls through to the control type's
// default handler by tail-jumping through gCtrlEvtFcnTbl, indexed by ctrl_t::wType at
// +0x22. That global is emitted as `void *[20]`, hence the cast to PFN_CTRL_EVT.
BOOL ctrl_btn_ok_ctrl(void *pArg0, void *pArg1)
{
  ctrl_t *pCtrl = static_cast<ctrl_t *>(pArg0);
  dlgevt_t *pEvent = static_cast<dlgevt_t *>(pArg1);

  if (pEvent->wTag == 0x0E && pEvent->dwId == 0x0D && sgnChatIsCreator == 0)
    return TRUE;

  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl, pEvent);
}

// 0x450D80, 283 B. The chat room's poll while the network layer settles:
// leave on a kick or a lobby close, refresh the slots, and show the password
// line once the game info has none.
// Hyperion: the dialog as an intptr_t, as the dialog loop passes it (PFN_MODALEXTRA);
// an int cut the pointer at 64 bits.
void gluChat_poll(intptr_t nArg0)
{
  int nState;

  // Hyperion: a LAN room's match, its lobby played for its players. A Cancel it
  // pressed took the poll off the chat room, as 1.17's own Cancel does, and the pass ends there:
  // the network it left would raise an error whose popup has no room left under it.
  PortLanLobbyTick();
  if (sgpChatDlg != NULL && sgpChatDlg->pfnDlgProc == NULL)
    return;

  switch (sub_474F00(reinterpret_cast<intptr_t>(&nState))) {
    case 0x4B:
      sub_451DA0(nArg0);
      dword_667C1C = 0;
      return;
    case 0x53:
      sub_450980(0, 0x22C);
      return;
  }

  if (dword_667C20 != 0) {
    sub_474E60();
    gluChat_mouse_moved(nArg0, 0, 1);
    dword_667C30 = 0;
    sub_450980(sgnChatKickStrId, 0x22D);
    return;
  }

  if (dword_667C1C != 0) {
    sub_451DA0(nArg0);
    dword_667C1C = 0;
  }
  if (dword_667C28 != 0) {
    char szInfo[0x18] = {0};

    SNetGetGameInfo(2, szInfo, 0x18, NULL);
    if (szInfo[0] == 0) {
      gluChat_update_password(nArg0);
      dword_667C28 = 0;
    }
  }
}

// 0x450EA0, 77 B. Leave the chat room with result 0x22D unless the network
// layer is still busy, in which case gluChat_poll is installed to poll.
void gluChat_start_poll(intptr_t nArg0)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);

  if (sub_474C60() == 0)
    sub_450980(0, 0x22D);
  else
    pDlg->pfnDlgProc = reinterpret_cast<void *>(gluChat_poll);
}

// 0x450EF0, 435 B. Fill the game-info text controls: players, template,
// parameter, subtype, creator, map and game speed.
void gluChat_update_game_info(intptr_t nArg0)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  char *pszTemplate;
  char *pszParam;
  int nSpeed;
  int i;

  psnprintf(byte_667C4C, 0x23, glues_get_str(0x20), word_6728F0, word_6728F2);
  pszTemplate = gametype_find_template_name(byte_6728F8, byte_6728F9);
  pszParam = gametype_find_param_name(byte_6728F8, byte_6728F9);
  if (pszParam != NULL && *pszParam != 0) {
    const TGAMEPARAMS *pParams;

    SStrCopy(byte_667C70, pszParam, 0x20);
    SStrPack(byte_667C70, ":", 0x20);
    pParams = gametype_find_template(byte_6728F8, byte_6728F9, word_6728FA);
    if (pParams != NULL) {
      int nMax = byte_6728F5;

      if (dword_6728EC != 0)
        nMax += byte_672903;
      gametype_get_subtype_name(byte_667C90, 0x400, pParams, nMax);
    } else {
      byte_667C90[0] = 0;
    }
  } else {
    byte_667C70[0] = 0;
    byte_667C90[0] = 0;
  }

  if (gPlayback != 0) {
    byte_667C70[0] = 0;
    SStrCopy(byte_667C90, glues_get_str(0xB2), 0x400);
  }
  glue_set_text(pDlg, 0x11, byte_6728D4);
  glue_set_text(pDlg, 0x14, byte_67291D);
  glue_set_text(pDlg, 0x12, pszTemplate);
  glue_set_text(pDlg, 0x0E, byte_667C70);
  glue_set_text(pDlg, 0x13, byte_667C90);
  glue_set_text(pDlg, 0x15, byte_667C4C);

  if (gPlayback != 0)
    nSpeed = replay_get_initial_game_speed();
  else
    nSpeed = byte_6728F6;
  for (i = 0; i < word_5039FC; i++) {
    if (sgGameSpeedStrs_g[i].nSpeed == nSpeed) {
      glue_set_text(pDlg, 0x16, glues_get_str(sgGameSpeedStrs_g[i].nStrId));
      return;
    }
  }
}

// 0x4510B0, 134 B. Show the game-password line and its label when the game has
// one, hide both when it does not.
void gluChat_update_password(intptr_t nArg0)
{
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_t *pCtrl;

  SNetGetGameInfo(2, byte_667C34, 0x18, NULL);
  if (byte_667C34[0] != 0) {
    glue_set_text(pDlg, 0x10, byte_667C34);
    pCtrl = ctrl_find_ndx(pDlg, 0x0C);
    *reinterpret_cast<dlg_word_t *>(pCtrl->_unmapped24) = 1;
    ctrl_show(pCtrl);
    pCtrl = ctrl_find_ndx(pDlg, 0x10);
    *reinterpret_cast<dlg_word_t *>(pCtrl->_unmapped24) = 1;
    ctrl_show(pCtrl);
  } else {
    ctrl_hide(ctrl_find_ndx(pDlg, 0x0C));
    ctrl_hide(ctrl_find_ndx(pDlg, 0x10));
  }
}

// 0x451140, 447 B. Append a chat line to the message list, colored by kind,
// keeping at most 0x40 lines and wrapping at a space with a three-space
// indent when the line is wider than the list.
void gluChat_write_msg(intptr_t nArg0, int nArg1, int nArg2)
{
  const char *pszText = reinterpret_cast<const char *>(nArg0);
  unsigned int nCount;
  BYTE bColor;
  int nLen;
  int nWidth;
  int nMax;

  for (nCount = lbox_get_count(sgpChatMsgList); nCount >= 0x40; nCount--)
    lbox_remove_item(sgpChatMsgList, 0);

  // Four explicit cases: folding case 0 into the default lets MSVC replace the
  // table with a setbe computation.
  switch (nArg1) {
    case 0:
      bColor = 2;
      break;
    case 1:
      bColor = 6;
      break;
    case 2:
      bColor = 4;
      break;
    case 3:
      bColor = 4;
      break;
    default:
      bColor = 2;
      break;
  }

  dlg_set_font(reinterpret_cast<ctrl_t *>(sgpChatMsgList));
  nLen = (int)strlen(pszText);
  nWidth = font_strlen(pszText);
  nMax = lbox_get_scroll(sgpChatMsgList) - gbFontMaxWdt;
  if (nWidth < nMax) {
    BYTE bItem = static_cast<BYTE>(lbox_add_item(reinterpret_cast<ctrl_t *>(sgpChatMsgList),
                                                 pszText, static_cast<BYTE>(bColor | 0x10)));

    if (bItem == 0xFF)
      return;
    lbox_set_selection(sgpChatMsgList, bItem);
  } else {
    char *pszBuf = static_cast<char *>(SMemAlloc(nLen + 1, "gluChat.cpp", 0xC1, 0));
    char *pszLine;

    strcpy(pszBuf, pszText);
    pszLine = pszBuf;
    for (;;) {
      int nCut = string_width(pszLine, nMax);
      char *pszCut;
      char cSaved;
      BYTE bItem;

      if (pszLine[nCut] != 0) {
        int i;

        for (i = nCut; i > 3; i--) {
          if (pszLine[i] == ' ') {
            nCut = i;
            break;
          }
        }
      }
      pszCut = pszLine + nCut;
      cSaved = *pszCut;
      *pszCut = 0;
      // A failed add returns without the list refresh and leaks the buffer,
      // as the oracle does.
      bItem = static_cast<BYTE>(lbox_add_item(reinterpret_cast<ctrl_t *>(sgpChatMsgList),
                                              pszLine, static_cast<BYTE>(bColor | 0x10)));
      if (bItem == 0xFF)
        return;
      lbox_set_selection(sgpChatMsgList, bItem);
      if (pszLine + nCut - pszBuf == nLen) {
        SMemFree(pszBuf, "gluChat.cpp", 0xE5, 0);
        break;
      }
      *pszCut = cSaved;
      if (cSaved == ' ') {
        do
          pszCut++;
        while (*pszCut == ' ');
      }
      pszCut[-2] = ' ';
      pszCut[-1] = ' ';
      pszLine = pszCut - 3;
      *pszLine = ' ';
    }
  }
  ctrl_set_update(reinterpret_cast<ctrl_t *>(sgpChatMsgList));
}

// 0x451310, 67 B. The message-to-send edit control's event handler. The chained-subtract
// lowering (`sub eax, 0Eh` / `jz` / `dec eax` / `jnz`) is a switch over wTag 14 and 15,
// not an if-chain, which would give two `cmp`s. The last case tested falls through into
// its own body, so case 15's arm comes first in the oracle and case 14's follows.
BOOL ctrl_edit_msgtosend_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  switch (pEvent->wTag) {
    case 0x0E:
      if (pEvent->dwId == 0x0A) {
        pCtrl->dwFlags |= 0x20400;
        sgpChatEditCtrl = pCtrl;
        edit_set_recalc(pCtrl, 3);
      }
      break;
    case 0x0F:
      ctrl_edit_msgtosend_char(nArg0, nArg1);
      return TRUE;
  }

  return edit_ctrl_edit(pCtrl, pEvent);
}

// 0x451360, 100 B. The message-to-send edit control's character event: it refuses a
// keystroke once the pending text reaches 0x77 characters, unless the key is backspace,
// and refuses a leading space on an empty buffer. Everything else goes to the generic
// edit handler.
//
// The long-buffer case is the `else`: the oracle's `jb` and `je` jump forward to the
// pass-through block, so the dword_665F58 block is the fallthrough. ctrl_t::pData is
// loaded once and still live at the empty test forty bytes later, which one local
// reproduces, and the WORD reads at the event's +0x08 are the keyboard arm of dlgevt_t's
// union slot, which dialog.h leaves unnamed.
void ctrl_edit_msgtosend_char(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);
  const char *pszText;

  dlg_set_font(pCtrl);
  pszText = static_cast<const char *>(pCtrl->pData);

  if (strlen(pszText) < 0x77 ||
      *reinterpret_cast<const unsigned short *>(pEvent->_unmapped08) == 8) {
    if (*reinterpret_cast<const unsigned short *>(pEvent->_unmapped08) != 0x20) {
      edit_ctrl_edit(pCtrl, pEvent);
      return;
    }
    if (pszText[0] != 0)
      edit_ctrl_edit(pCtrl, pEvent);
    return;
  }

  if (dword_665F58 == 1)
    dword_665F5C = 1;
}

// 0x4513D0, 121 B. The message listbox's event handler: a switch inside an `if`, the
// inner dispatch a dense byte-index table over dwId 0..11 feeding five arms. Case 10 is
// written first so that its `break` falls through into the shared tail instead of jumping
// to it, which the oracle's ascending arm addresses need; cases 0 and 1 each carry their
// own copy of that tail jump, which an explicit `return lbox_ctrl_list(...)` per arm
// gives. `or dl, -1h` is MSVC putting 0xFF in DL for the selection sentinel.
BOOL ctrl_listbox_msgs_ctrl_fcn(intptr_t nArg0, intptr_t nArg1)
{
  ctrl_t *pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 10:
        pCtrl->dwFlags |= 0x10020400;
        break;
      case 0:
        sgpChatMsgList = reinterpret_cast<lbox_t *>(pCtrl);
        lbox_set_selection(reinterpret_cast<lbox_t *>(pCtrl), 0xFF);
        lbox_set_recalc(reinterpret_cast<lbox_t *>(pCtrl), 1);
        break;
      case 1:
        lbox_remove_all(sgpChatMsgList);
        break;
      case 11:
        lbox_ctrl_list(pCtrl, pEvent);
        return TRUE;
    }
  }

  return lbox_ctrl_list(pCtrl, pEvent);
}

// 0x451470, 233 B. The chat room's modal runner: load the dialog out of rez\gluChat.bin,
// run it modally with gluChat_dlg_fcn, then branch on the modal result and tear the screen
// down. The casts on load_game_data and alloc_load_bmp are the same pair credits.cpp and
// replay.cpp use, and the trailing ("gluChat.cpp", 0x475) is the file and line dlg_load
// takes -- 0x4ED6A4 in the oracle's .rdata is that string.
//
// The outer dispatch is a switch, not an if-chain: the chained-subtract lowering over 8,
// 0x22C and 0x22D is what an if-chain's three `cmp`s would not give. The inner
// NetworkMode() dispatch is a compare-and-branch chain whose default body is emitted
// inline before its named arms, and the multi-character constants are the oracle's own
// immediates.
void do_gluChat_dlg()
{
  int nResult;

  dword_667C1C = 0;
  dword_667C20 = 0;
  dword_5039F8 = 1;
  dword_667C28 = 0;

  sgpChatDlg = dlg_load("rez\\gluChat.bin",
                        reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                        reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                        "gluChat.cpp", 0x475);
  sub_4520C0(reinterpret_cast<intptr_t>(sgpChatDlg));
  init_lang_imm(1);

  nResult = dlg_run_modal(sgpChatDlg, 0,
                          reinterpret_cast<PFN_CTRL_EVT>(gluChat_dlg_fcn));
  init_lang_imm(0);

  switch (nResult) {
    case 8:
    case 0x22D:
      switch (NetworkMode()) {
        case 'BNET':
          glGluesMode = 4;
          break;
        case 'MDMX':
        case 'MODM':
          glGluesMode = 0x14;
          break;
        default:
          glGluesMode = 0x0A;
          break;
      }
      gPlayback = 0;
      map_free();
      // Hyperion: a LAN room's match left before it started: the network left and home
      // next, where the room is
      if (PortLanChatLeft()) {
        NetworkShutdown();
        gbMultiPlayerMode = 0;
        glGluesMode = 0;
      }
      break;
    case 0x22C:
      break;
    default:
      glGluesMode = 0;
      break;
  }

  glues_clear_all();
}

// 0x451560, 223 B. Format an incoming chat line and hand it to the list. Four kinds,
// decided by the sender id: our own storm id -> 0, any other id below 8 -> 1, exactly
// 8 -> 2 and -1 -> 3, anything else dropped. Kinds 0 and 1 get a "<name>: " prefix; kinds
// 2 and 3 take the message verbatim.
//
// The two net_player_get_name calls are the oracle's: MSVC does not CSE a call, so the
// strlen and the copy each fetch the name again. Both id compares are unsigned, and the
// `-1` one has to be -- spelled signed, MSVC knows EBX holds -1 and rematerialises the
// next constant off it, `lea edi, [ebx+4h]` where the oracle has `mov edi, 3h`. The four
// arms are one chain split on `nKind < 2`, which keeps nKind in a single live range and
// in EDI on every path; giving the 8 and -1 arms their own copy and write pair is 222
// bytes against the oracle's 223, and a sunk initializer 234. MSVC duplicates the shared
// gluChat_write_msg call back into both arms itself, which is where the oracle's two call
// sites come from.
void on_recv_chat_from(int nArg0, intptr_t nArg1)
{
  char szBuf[0x94];
  const char *pszMsg = reinterpret_cast<const char *>(nArg1);
  int nKind;

  if (static_cast<DWORD>(nArg0) == g_LocalStormID)
    nKind = 0;
  else if (static_cast<DWORD>(nArg0) < 8)
    nKind = 1;
  else if (static_cast<DWORD>(nArg0) == 8)
    nKind = 2;
  else if (static_cast<DWORD>(nArg0) == 0xFFFFFFFFu)
    nKind = 3;
  else
    return;

  // Hyperion: Settings' Mature language filter, on other players' lines (port_chatfilter.cpp)
  char szClean[0x80];
  if (nKind == 1)
    pszMsg = PortChatClean(pszMsg, szClean, sizeof szClean);

  if (nKind < 2) {
    size_t nLen = strlen(net_player_get_name(static_cast<BYTE>(nArg0)));
    strcpy(szBuf, net_player_get_name(static_cast<BYTE>(nArg0)));
    strcat(szBuf, ": ");
    SStrCopy(szBuf + nLen + 2, pszMsg, 0x78);
  } else {
    SStrCopy(szBuf, pszMsg, 0x78);
  }

  gluChat_write_msg(reinterpret_cast<intptr_t>(szBuf), nKind, 0);
}

// 0x451640, 59 B. Ask the user to confirm kicking a player. `push ecx` is the first
// instruction after the frame because pszName is the last vararg and varargs go down
// right to left, before glues_get_str is called at all; the closing `movzx eax, al`
// widens do_pok_cancel_dlg's BYTE into the BOOL all five callers read as 32 bits.
BOOL ConfirmKick(const char *pszName)
{
  char szBuf[0x22C];

  psnprintf(szBuf, 0x22C, glues_get_str(0x7E), pszName);
  return do_pok_cancel_dlg(reinterpret_cast<intptr_t>(szBuf));
}

// 0x451680, 232 B. Show the start countdown: bSecs seconds left, with the
// cancel button grayed at 3 and the "game starting" string at 1.
void updateChrmCountdown(BYTE bSecs)
{
  ctrl_t *pText;
  int nStrId;

  if (dword_667C30 == 1) {
    gluChat_mouse_moved(reinterpret_cast<intptr_t>(sgpChatDlg), 0, 0);
    dword_667C30 = 0;
  }
  // Hyperion: a LAN room's match counts down unseen, and Hyperion's countdown beeps over
  // its map instead (port_intro.cpp)
  if (!PortLanLobbyUnseen())
    sub_49E740(0xAA);
  if (dword_5039F8 != 0) {
    sub_451890(reinterpret_cast<intptr_t>(sgpChatDlg));
    dword_5039F8 = 0;
  }
  if (bSecs == 3) {
    ctrl_t *pCancel = ctrl_find_ndx(sgpChatDlg, 8);

    if (pCancel != NULL)
      ctrl_gray(pCancel);
  }
  ctrl_show(ctrl_find_ndx(sgpChatDlg, 0x17));
  pText = ctrl_find_ndx(sgpChatDlg, 0x18);
  ctrl_show(pText);
  if (bSecs == 1) {
    dword_5039F8 = 1;
    nStrId = 0xCA;
  } else {
    nStrId = 0x82;
  }
  psnprintf(byte_668090, 0x100, "%d %s", bSecs, glues_get_str(nStrId));
  ctrl_set_text(pText, byte_668090);
}

// 0x451770, 11 B. Hand the live chat dialog to the slot-table refresher.
//
// A bare load-and-tail-jump, which is what a one-statement void function whose
// only statement is a call gives.
void sub_451770()
{
  sub_451AD0(reinterpret_cast<intptr_t>(sgpChatDlg));
}

// 0x451780, 11 B. Raise the 0x667C28 flag. The definition belongs here, as the oracle's
// TU map has it, while the declaration is gluChatSlot.h's; gluChat.h does not repeat it.
void sub_451780()
{
  dword_667C28 = 1;
}

// 0x451790, 11 B. Raise the 0x667C1C flag. The lobby's redraw-request entry --
// seven landed net_chat_cmds.cpp receivers call it.
void sub_451790()
{
  dword_667C1C = 1;
}

// 0x4517A0, 34 B. Hide control 0x17 and the control that follows it. The
// `mov ecx, dword ptr [esi]` between the two calls is ctrl_t::pNext read off the control
// the first call just hid, so the second hide is its sibling rather than a second lookup,
// and ESI keeps that control live across the first call, which a named local gives.
void sub_4517A0()
{
  ctrl_t *pCtrl = ctrl_find_ndx(sgpChatDlg, 0x17);

  ctrl_hide(pCtrl);
  ctrl_hide(pCtrl->pNext);
}
