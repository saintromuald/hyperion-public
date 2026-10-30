// replay.cpp -- replay recording and playback: the replay header and slot blob
// at sgReplayHeader_g, the playback-speed orders, and the Load Replay dialog.
// Functions are in oracle VA order.
//
// strip_trailing (0x4814B0) is a function of its own and is also written out
// inside replay_dlg_deactivate, as 1.17 has it twice.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/replay.h"
#include "../h/gds_log.h"
#include "../h/dlg_effects.h"
#include "../h/glues.h"
#include "../h/save_dlg.h"  // gray_delete_btn -- declared where it is defined
#include "../h/opt_dlgs.h"  // dfl_load_bmp, opt_get_tile_grp, sub_47C110
#include "../gen/globals117.h"
#include "../h/net_cmds.h"
#include "../h/Storm.h"
#include "../h/saveload.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"   // font_set
#include "../h/gds_font.h"   // string_width
#include "../h/dlg_lbox.h"
#include "../h/victory.h"
#include "../h/TrigAct.h"
#include "../h/dlg_ctrl.h"
#include "../h/dlg_lbox.h"
#include "../h/minimap.h"   // btn_ctrl_pushbutton
#include "../h/event_Win.h" // init_lang_imm
#include "../h/gameloop.h"  // sub_445110 -- reseated when gameloop.cpp opened
#include "../h/net_record.h"   // net_record_init, sub_478970
#include "../h/map_load.h"     // GetCurrentMap
#include "../h/maphdr.h"       // get_map_chk
#include "../h/okcancel.h"     // modeless_ok_dlg, modeless_okcancel_dlg

#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../h/port_hxreplay.h"  // Hyperion: Hyperion's replays
#include "../port/tools/port_bench.h"  // Hyperion: PortBenchPlaying, PortBenchMaking, PortBenchPatchHeader
#include "../port/tools/port_benchview.h"  // Hyperion: PortBenchViewPause
#include "../port/gameplay/port_watchgame.h"  // Hyperion: PortWatchMaking, PortWatchPatchHeader

// KERNEL32's GetFileAttributesA and GetLastError, which sub_4802C0 calls through
// 1.17's IAT, declared as dlg_dlg.cpp declares GetTickCount.
extern "C" __declspec(dllimport) DWORD __stdcall GetFileAttributesA(const char *pszName);
extern "C" __declspec(dllimport) DWORD __stdcall GetLastError(void);

// Storm imports, by ordinal: SStrPack (503), SFileGetUserDataPath (296) and
// SStrCmpI (509), each __stdcall with three arguments. SStrPack is declared in
// src117/h/Storm.h.
extern "C" {
// Hyperion: as port_stormfile.cpp defines it
DWORD  __stdcall SFileGetUserDataPath(char *pszBuf, DWORD cchBuf, DWORD dwFlags);
int    __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
}

// Types this TU needs.

// The rep_lb_types_g record: 0x20 bytes, a BYTE at +0x18 and a WORD at +0x1C.
struct lb_type_t
{
  /*0x00*/ char  szName[0x18];
  /*0x18*/ BYTE  bAction;
  /*0x19*/ BYTE  _pad19[3];
  /*0x1C*/ WORD  wParam;
  /*0x1E*/ WORD  _pad1E;
};

// The rep_cmds_g record: 0x10 bytes, the command name inline at +0 and its
// handler at +0x0C.
struct rep_cmd_t
{
  /*0x00*/ char szName[0x0C];
  /*0x0C*/ int (__fastcall *pfn)(char *pszArgs);
};

// Callees no header declares. act_leaderboard is declared in TrigAct.h.
void __fastcall statlb_refresh(int nArg0);
BOOL __fastcall pfdelete(char *pszPath, BOOL bAlsoReadOnly);
// The globals this TU reaches, dual-spelled, one #ifndef per name. The names
// are Blizzard's; an address that has none is spelled `oc_*` (dword_67CCA1,
// byte_67CF11, byte_67CF19, byte_67CF1C, dword_67CC70).

#ifdef OC_DLL_MODE
#define sgdwSpeedMul_g (*reinterpret_cast<int *>(OC_VA(0x508E34u)))
#else
extern int sgdwSpeedMul_g;
#endif

#ifdef OC_DLL_MODE
#define sgReplayTicks_g (reinterpret_cast<DWORD *>(OC_VA(0x4EF660u)))
#else
extern DWORD sgReplayTicks_g[10];
#endif

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgbReplayPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC7Cu)))
#else
extern int sgbReplayPaused_g;
#endif

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgdwReplaySeed_g (*reinterpret_cast<int *>(OC_VA(0x67CCA8u)))
#else
extern int sgdwReplaySeed_g;
#endif

// dword_67CCA1 -- the replay's total frame count, the divisor in
// replay_get_progress and what replay_set_total_frames stores. Unaligned: the
// header around it is packed.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_dwReplayTotalFrames (*reinterpret_cast<unsigned int *>(OC_VA(0x67CCA1u)))
#else
extern unsigned int oc_dwReplayTotalFrames;
#endif

// sgReplayHeader_g + 0x25 and + 0x26, two BYTEs, spelled as scalars of their
// own rather than through the header blob.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_bReplayMaxPlayers (*reinterpret_cast<BYTE *>(OC_VA(0x67CCD9u)))
#else
extern BYTE oc_bReplayMaxPlayers;
#endif

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_bReplayInitialSpeed (*reinterpret_cast<BYTE *>(OC_VA(0x67CCDAu)))
#else
extern BYTE oc_bReplayInitialSpeed;
#endif

// sgReplaySlotMap_g -- 8 dwords, 0x67CEF1..0x67CF11, from the slot records' end
// to byte_67CF11.
#ifdef OC_DLL_MODE
#define sgReplaySlotMap_g (reinterpret_cast<DWORD *>(OC_VA(0x67CEF1u)))
#else
extern DWORD sgReplaySlotMap_g[8] OC_UNALIGNED;
#endif

// byte_67CF11 -- 8 bytes, copied whole both ways by replay_get_selectable_races
// and replay_set_selectable_races.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_ReplayFlags8 (reinterpret_cast<DWORD *>(OC_VA(0x67CF11u)))
#else
extern DWORD oc_ReplayFlags8[2];
#endif

// The current playback frame, a DWORD no source names.
#ifdef OC_DLL_MODE
#define oc_dwReplayFrame (*reinterpret_cast<unsigned int *>(OC_VA(0x52651Cu)))
#else
extern unsigned int oc_dwReplayFrame;
#endif

// dword_67CC70 -- the list box control the replay dialog fills.
#ifdef OC_DLL_MODE
#define oc_pReplayListbox (*reinterpret_cast<ctrl_t **>(OC_VA(0x67CC70u)))
#else
extern ctrl_t *oc_pReplayListbox;
#endif


// byte_67CF1C -- the replay name edit buffer, char[0x1C], which edit_get_text
// fills, strip_trailing walks and delete_replay takes as the file name.
#ifdef OC_DLL_MODE
#define oc_szReplayName (reinterpret_cast<char *>(OC_VA(0x67CF1Cu)))
#else
extern char oc_szReplayName[0x1C] OC_UNALIGNED;
#endif

// dword_508E38 -- the last progress value the status bar was told about;
// sub_480FB0 compares and stores the unsigned quotient.
#ifdef OC_DLL_MODE
#define oc_dwReplayProgressShown (*reinterpret_cast<DWORD *>(OC_VA(0x508E38u)))
#else
extern DWORD oc_dwReplayProgressShown;
#endif

// sgbReplayShowPaused_g@0x67CC80 -- the blink phase of the PAUSED overlay, a
// full dword.
#ifdef OC_DLL_MODE
#define sgbReplayShowPaused_g (*reinterpret_cast<int *>(OC_VA(0x67CC80u)))
#else
extern int sgbReplayShowPaused_g;
#endif

// dword_67CC8C -- the GetTickCount value of the last blink; the difference from
// it is compared signed.
#ifdef OC_DLL_MODE
#define oc_nReplayBlinkTick (*reinterpret_cast<int *>(OC_VA(0x67CC8Cu)))
#else
extern int oc_nReplayBlinkTick;
#endif

// sgdwUpdatesPerFrame_g@0x50335C, gameloop.cpp's.
#ifdef OC_DLL_MODE
#define sgdwUpdatesPerFrame_g (*reinterpret_cast<DWORD *>(OC_VA(0x50335Cu)))
#else
extern DWORD sgdwUpdatesPerFrame_g;
#endif

// dword_67CF40 -- the scaled turn-rate table replay_set_speed fills and hands to
// sub_445110 in ECX: seven dwords written, eight up to sgpRpMap@0x67CF60.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_ReplayTurnRates (reinterpret_cast<DWORD *>(OC_VA(0x67CF40u)))
#else
extern DWORD oc_ReplayTurnRates[8];
#endif

// rep_cmds_g (0x4EF688) and rep_lb_types_g (0x4EF6A8), .rdata tables,
// dual-spelled.

// Two entries, up to rep_lb_types_g.
#ifdef OC_DLL_MODE
#define rep_cmds_g (reinterpret_cast<const rep_cmd_t *>(OC_VA(0x4EF688u)))
#else
extern const rep_cmd_t rep_cmds_g[2];
#endif

// Twelve entries of 0x20, up to 0x4EF828.
#ifdef OC_DLL_MODE
#define rep_lb_types_g (reinterpret_cast<const lb_type_t *>(OC_VA(0x4EF6A8u)))
#else
extern const lb_type_t rep_lb_types_g[12] OC_UNALIGNED;
#endif

// sgpMap -- the replay's map, which map_free hands to SMemFree and clears.
#ifdef OC_DLL_MODE
#define sgpMap (*reinterpret_cast<void **>(OC_VA(0x67CC78u)))
#else
extern void *sgpMap;
#endif

// word_67CCA5 -- a WORD: GetReplayMission loads it as 16 bits and its one
// caller uses 16.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_wReplayHdrA5 (*reinterpret_cast<WORD *>(OC_VA(0x67CCA5u)))
#else
extern WORD oc_wReplayHdrA5;
#endif

// The 13-byte block at 0x67CCA7, from word_67CCA5's end to sgReplayHeader_g,
// that sub_480540 and sub_4807F0 copy whole (8, 4 and 1 bytes). It overlaps
// sgdwReplaySeed_g at 0x67CCA8, so it is an opaque byte block, not fields.
#pragma pack(push, 1)
struct replay_seed_t
{
  BYTE _raw[13];
};
#pragma pack(pop)

#ifdef OC_DLL_MODE
#define oc_ReplaySeedBlob (*reinterpret_cast<replay_seed_t *>(OC_VA(0x67CCA7u)))
#else
extern replay_seed_t oc_ReplaySeedBlob;
#endif

// sgReplayHeader_g -- 0x23D bytes: the 141-byte game header
// replay_assign_gamedata copies, then the 12 x 36-byte player slots (0x67CD41,
// an interior offset, not a global of its own), up to sgReplaySlotMap_g. Both
// regions stay opaque.
#pragma pack(push, 1)
struct replay_slot_t
{
  BYTE _raw[36];
};

struct replay_header_t
{
  BYTE          _head[0x8D];   /*0x000*/  // 141 B
  replay_slot_t slots[12];     /*0x08D*/  // 432 B
};
#pragma pack(pop)

static_assert(sizeof(replay_slot_t) == 36,
              "replay_slot_t: the copy loop's 36-byte element stride");
static_assert(sizeof(replay_header_t) == 573,
              "replay_header_t: 0x67CCB4 .. 0x67CEF1 (sgReplaySlotMap_g)");
static_assert(offsetof(replay_header_t, slots) == 0x8D,
              "replay_header_t::slots: 0x67CD41 - 0x67CCB4");

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgReplayHeader_g (*reinterpret_cast<replay_header_t *>(OC_VA(0x67CCB4u)))
#else
extern replay_header_t sgReplayHeader_g;
#endif

// lbox_add_item, edit_get_text and edit_ctrl_edit are declared in
// src117/h/save_dlg.h, included above.

// GetTickCount, KERNEL32's, which sub_480FB0 calls through IAT slot 0x4EB2FC;
// declared as dlg_dlg.cpp declares it.
extern "C" __declspec(dllimport) ULONG __stdcall GetTickCount(void);

// The bodies, in oracle VA order.

// 0x4805B0, 6 B.
int replay_get_seed()
{
  return sgdwReplaySeed_g;
}

// 0x4805C0, 8 B.
int replay_get_initial_game_speed()
{
  return oc_bReplayInitialSpeed;
}

// 0x4805E0, 8 B.
int replay_get_max_players()
{
  return oc_bReplayMaxPlayers;
}

// 0x480630, 64 B. Eight dword copies from the unaligned source, not a memcpy,
// as 1.17 copies it.
void get_replay_player_colors(void *pOut)
{
  DWORD *pDst = static_cast<DWORD *>(pOut);
  pDst[0] = sgReplaySlotMap_g[0];
  pDst[1] = sgReplaySlotMap_g[1];
  pDst[2] = sgReplaySlotMap_g[2];
  pDst[3] = sgReplaySlotMap_g[3];
  pDst[4] = sgReplaySlotMap_g[4];
  pDst[5] = sgReplaySlotMap_g[5];
  pDst[6] = sgReplaySlotMap_g[6];
  pDst[7] = sgReplaySlotMap_g[7];
}

// 0x480680, 16 B.
void replay_get_selectable_races(void *pOut)
{
  DWORD *pDst = static_cast<DWORD *>(pOut);
  pDst[0] = oc_ReplayFlags8[0];
  pDst[1] = oc_ReplayFlags8[1];
}

// 0x4808A0, 7 B.
void replay_set_seed(int nSeed)
{
  sgdwReplaySeed_g = nSeed;
}

// 0x4808B0, 7 B.
void replay_set_total_frames(int nTotalFrames)
{
  oc_dwReplayTotalFrames = static_cast<unsigned int>(nTotalFrames);
}

// 0x4808C0, 16 B.
void replay_set_selectable_races(const void *pIn)
{
  const DWORD *pSrc = static_cast<const DWORD *>(pIn);
  oc_ReplayFlags8[0] = pSrc[0];
  oc_ReplayFlags8[1] = pSrc[1];
}

// 0x480D70, 34 B. sgReplayTicks_g indexed by the recorded game speed, divided
// by 1000 unsigned.
int replay_dlg_elapsed_seconds()
{
  return static_cast<int>(sgReplayTicks_g[oc_bReplayInitialSpeed] *
                          oc_dwReplayFrame / 1000u);
}

// 0x480DA0, 6 B.
int replay_dlg_get_mult()
{
  return sgdwSpeedMul_g;
}

// 0x480E00, 19 B. The incoming ECX is pushed as the STACK argument and the two
// globals become the register pair.
void sub_480E00(int nArg0)
{
  replay_set_speed(gOptions, sgdwSpeedMul_g, nArg0);
}

// 0x480E20, 16 B. The division is unsigned.
int replay_get_progress()
{
  return static_cast<int>(oc_dwReplayFrame * 100u / oc_dwReplayTotalFrames);
}

// 0x480E30, 19 B.
void replay_init_speed()
{
  replay_set_speed(gOptions, 1, 0);
}

// 0x480F70, 32 B. The third argument is the truth value "not paused", not the
// flag itself.
void order_replay_play()
{
  // Hyperion: a benchmark run pauses as its overlay's Pause does, so the paused time is
  // left out of its numbers
  if (PortBenchPlaying()) {
    PortBenchViewPause();
    return;
  }
  // A BYTE local holds the truth value, widened at the call, as 1.17 does.
  send_net_cmd_SetReplaySpeed(gOptions, sgdwSpeedMul_g,
                              static_cast<BYTE>(sgbReplayPaused_g == 0));
}

// 0x480F90, 31 B. The bf_ shape: gate on gPlayback, then on IsGameHost, then
// return a truth value, written as one conjunction over a shared `return FALSE`.
BOOL __stdcall bf_replay_play(int)
{
  if (gPlayback != 0 && IsGameHost())
    return TRUE;
  return FALSE;
}

// 0x481300, 23 B. __stdcall: `ret 4h` with the item in a stack slot.
void __stdcall sub_481300(const char *pszItem)
{
  lbox_add_item(oc_pReplayListbox, pszItem, 0x10);
}

// 0x4814B0, 35 B. An inline strlen, then a walk back over trailing spaces. The
// index is signed: unsigned, it would wrap and the guard never fire.
void strip_trailing(char *pszText)
{
  int nAt = static_cast<int>(strlen(pszText));
  --nAt;
  while (nAt >= 0)
  {
    if (pszText[nAt] != ' ')
      break;
    pszText[nAt] = '\0';
    --nAt;
  }
}


// Callees no other header declares.

// 0x4C8110, 54 B. ECX = the edit control, EDX = the text; it tail-jumps
// ctrl_set_update.
void __fastcall edit_set_text(ctrl_t *pCtrl, const char *pszText);

// lbox_ctrl_list is declared in src117/h/dlg_lbox.h.

// gray_delete_btn (0x49B360) is declared in src117/h/save_dlg.h.

// The edit control's own fields in ctrl_t, reached by cast through dialog.h's
// filler windows, which name only the shared head and the dialog arm.

// +0x37, a BYTE sub_481450 and replaydlg_listbox_ctrl_init clear right after
// edit_set_text: the edit control's caret or selection state.
static inline dlg_byte_t *edit_byte37(ctrl_t *pCtrl) {
  return &pCtrl->_unmapped36[1];
}

// +0x38, a BYTE replaydlg_edit_name_fcn sets to 3 when the control is created.
static inline dlg_byte_t *edit_byte38(ctrl_t *pCtrl) {
  return &pCtrl->_unmapped36[2];
}

// +0x3E on an edit control is a BYTE, not the dialog arm's pFocus pointer: the
// maximum text length.
static inline dlg_byte_t edit_max_len(const ctrl_t *pCtrl) {
  return *reinterpret_cast<const dlg_byte_t *>(&pCtrl->pFocus);
}

// dlgevt_t +0x08 as a WORD, the virtual key, as ctrl_dlg_char reads it.
static inline dlg_word_t evt_vkey(const dlgevt_t *pEvent) {
  return *reinterpret_cast<const dlg_word_t *>(&pEvent->_unmapped08[0]);
}

// The bodies, in oracle VA order.

// 0x481160, 55 B.
BOOL replaydlg_btn_delete_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 2:
      delete_replay(oc_szReplayName);
      ctrl_evt(pCtrl->pParent, pEvent);
      return TRUE;
    }
  }
  return btn_ctrl_pushbutton(pCtrl, pEvent);
}

// 0x4811A0, 129 B. A two-level jump table over dwId 0..10. The case order below
// is 1.17's block order: init (case 10) comes first.
int replay_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E)
  {
    switch (pEvent->dwId)
    {
    case 10:
      replay_dlg_init(pCtrl);
      break;
    case 0:
      replay_dlg_activate(pCtrl);
      break;
    case 2:
      {
        ctrl_t *pEdit = ctrl_find_ndx(pCtrl, 2);
        if (pEdit != NULL)
        {
          edit_get_text(pEdit, oc_szReplayName);
          strip_trailing(oc_szReplayName);
        }
      }
      break;
    case 1:
      replay_dlg_deactivate(pCtrl);
      break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// 0x481250, 161 B. The shared `jmp edit_ctrl_edit` tail is textually last; the
// 0x0F arm is the only one with its own `return TRUE`.
BOOL replaydlg_edit_name_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  switch (pEvent->wTag)
  {
  case 0:
    if (evt_vkey(pEvent) == 0x2E)
    {
      dlg_byte_t nMax = edit_max_len(pCtrl);
      if (nMax < strlen(static_cast<const char *>(pCtrl->pData)))
      {
        ctrl_t *pBox = ctrl_find_ndx(pCtrl, 1);
        if (lbox_get_selection(reinterpret_cast<lbox_t *>(pBox)) != 0xFF)
        {
          ctrl_set_val(pBox, 0xFF);
          ctrl_set_update(pBox);
          gray_delete_btn(pBox);
        }
      }
    }
    break;
  case 0x0E:
    if (pEvent->dwId == 0)
    {
      pCtrl->dwFlags |= 0x80000000;
      *edit_byte38(pCtrl) = 3;
    }
    break;
  case 0x0F:
    replaydlg_edit_evn_char(pCtrl, pEvent);
    return TRUE;
  }
  return edit_ctrl_edit(pCtrl, pEvent);
}

// 0x481320, 67 B.
BOOL replaydlg_listbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E)
  {
    if (pEvent->dwId != 0)
    {
      if (pEvent->dwId == 0x0B)
      {
        lbox_ctrl_list(pCtrl, pEvent);
        sub_481450(pCtrl);
        gray_delete_btn(pCtrl);
        return TRUE;
      }
    }
    else
    {
      replaydlg_listbox_ctrl_init(pCtrl);
    }
  }
  return lbox_ctrl_list(pCtrl, pEvent);
}

// 0x481370, 211 B. gray_delete_btn and sub_481450 are both written out here, as
// 1.17 inlines them.
void replaydlg_listbox_ctrl_init(ctrl_t *pDlg)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pDlg);

  lbox_set_recalc(pBox, 0);
  oc_pReplayListbox = pDlg;
  sub_4803D0(&sub_481300);
  oc_pReplayListbox = NULL;
  lbox_set_recalc(pBox, 1);

  if (lbox_get_count(pBox) == 0)
    ctrl_gray(ctrl_find_ndx(pDlg, -2));

  // gray_delete_btn(pDlg), written out.
  {
    ctrl_t *pBtn = ctrl_find_ndx(pDlg, 3);
    if (pBtn != NULL)
    {
      if (lbox_get_count(pBox) != 0 && lbox_get_selection(pBox) != 0xFF)
        ctrl_ungray(pBtn);
      else
        ctrl_gray(pBtn);
    }
  }

  // sub_481450(pDlg), written out.
  {
    ctrl_t *pEdit = ctrl_find_ndx(pDlg, 2);
    if (pEdit != NULL)
    {
      dlg_byte_t nSel = lbox_get_selection(pBox);
      if (nSel != 0xFF)
      {
        edit_set_text(pEdit,
                      static_cast<const char *>(lbox_get_item(pBox, nSel)));
        *edit_byte37(pEdit) = 0;
        ctrl_t *pBtn = ctrl_find_ndx(pDlg, -2);
        if (pBtn != NULL)
          ctrl_ungray(pBtn);
      }
    }
  }
}

// 0x481450, 81 B.
void sub_481450(ctrl_t *pDlg)
{
  lbox_t *pBox = reinterpret_cast<lbox_t *>(pDlg);
  ctrl_t *pEdit = ctrl_find_ndx(pDlg, 2);
  if (pEdit != NULL)
  {
    dlg_byte_t nSel = lbox_get_selection(pBox);
    if (nSel != 0xFF)
    {
      edit_set_text(pEdit,
                    static_cast<const char *>(lbox_get_item(pBox, nSel)));
      *edit_byte37(pEdit) = 0;
      ctrl_t *pBtn = ctrl_find_ndx(pDlg, -2);
      if (pBtn != NULL)
        ctrl_ungray(pBtn);
    }
  }
}

// Two more Storm imports: SStrPrintf (ordinal 578), __cdecl and variadic, and
// SFileGetUserDataPath (296), declared above.
extern "C" {
int __cdecl SStrPrintf(char *pszDest, size_t nDestSize, const char *pszFormat,
                       ...);
}

// 0x47FE60, 342 B. Copies the finished game's LastReplay.rep to a named replay
// file, both under the user data path. Its strings are 1.17's: the path format
// "%s\maps\replays\%s.%s" (0x4EE5E4), "LastReplay", "rep", and the owner tags
// "replay.cpp" and "delete". The format's backslashes are doubled below, or `\m`
// and `\%` would compile away; 0x321 (801) is 1.17's __LINE__.
BOOL copy_lastreplay_to(const char *pszName)
{
  char szUserPath[0x104];
  char szSrc[0x104];
  char szDst[0x104];
  DWORD dwRead;
  DWORD dwWritten;
  HANDLE hSrc;
  HANDLE hDst;
  void *pBuf;

  szUserPath[0] = '\0';
  SFileGetUserDataPath(szUserPath, 0x104, 0);

  SStrPrintf(szSrc, 0x104, "%s\\maps\\replays\\%s.%s", szUserPath, "LastReplay",
             "rep");
  SStrPrintf(szDst, 0x104, "%s\\maps\\replays\\%s.%s", szUserPath, pszName,
             "rep");

  hSrc = CreateFileA(szSrc, 0x80000000, 1, NULL, 3, 0x80, NULL);
  if (hSrc == INVALID_HANDLE_VALUE)
    return FALSE;

  hDst = CreateFileA(szDst, 0x40000000, 0, NULL, 2, 0x80, NULL);
  if (hDst == INVALID_HANDLE_VALUE)
    return FALSE;

  pBuf = SMemAlloc(0x4000, "replay.cpp", 0x321, 0);

  ReadFile(hSrc, pBuf, 0x4000, &dwRead, NULL);
  while (dwRead != 0)
  {
    WriteFile(hDst, pBuf, dwRead, &dwWritten, NULL);
    ReadFile(hSrc, pBuf, 0x4000, &dwRead, NULL);
  }

  if (pBuf != NULL)
    SMemFree(pBuf, "delete", -1, 0);

  CloseHandle(hSrc);
  CloseHandle(hDst);
  return TRUE;
}

// The playback speed orders, their button queries and the status tick.

// 0x480E50, 69 B. Three arms and one trailing call: the arms only adjust the
// two locals, as 1.17 has it. At speed 6 with the multiplier already at its
// floor, the first arm falls through to stepping the speed option down.
void order_replay_slowdown()
{
  if (sgbReplayPaused_g != 0)
    return;
  // Hyperion: a benchmark run steps its own speed, which it records
  if (PortBenchPlaying()) {
    PortBenchSpeedStep(-1);
    return;
  }

  DWORD dwSpeed = gOptions;
  int nMul = sgdwSpeedMul_g;

  if (dwSpeed == 6 && nMul > 1)
    nMul >>= 1;
  else if (dwSpeed != 0)
    --dwSpeed;
  else if (nMul < 1)
    nMul += nMul;

  send_net_cmd_SetReplaySpeed(dwSpeed, nMul, 0);
}

// 0x480EA0, 64 B. The tri-state button query: 1 enabled, -1 greyed, 0 hidden.
// There is one `return -1`, before `return TRUE`, with the two conditions
// merged by `||`; with two of them the compiler makes the last test branchless.
BOOL __stdcall bf_replay_slowdown(int)
{
  if (gPlayback != 0 && IsGameHost())
  {
    if ((gOptions == 0 && sgdwSpeedMul_g >= 1) || sgbReplayPaused_g != 0)
      return -1;
    return TRUE;
  }
  return FALSE;
}

// 0x480EE0, 68 B. The mirror of order_replay_slowdown, not symmetric with it:
// slowdown branches on `== 6` (the fast end) and speedup on `== 0` (the slow
// end), so the step arm here is `< 6` and the doubling arm is bounded by 0x10
// instead of 1. The speed compare is unsigned, so the local is a DWORD. It keeps
// three calls, as 1.17 does.
void order_replay_speedup()
{
  if (sgbReplayPaused_g != 0)
    return;
  // Hyperion: a benchmark run steps its own speed, which it records
  if (PortBenchPlaying()) {
    PortBenchSpeedStep(1);
    return;
  }

  DWORD dwSpeed = gOptions;
  int nMul = sgdwSpeedMul_g;

  if (dwSpeed == 0 && nMul > 1)
    send_net_cmd_SetReplaySpeed(dwSpeed, nMul >> 1, 0);
  else if (dwSpeed < 6)
    send_net_cmd_SetReplaySpeed(dwSpeed + 1, nMul, 0);
  else
  {
    if (nMul < 0x10)
      nMul += nMul;
    send_net_cmd_SetReplaySpeed(dwSpeed, nMul, 0);
  }
}

// 0x480F30, 64 B. bf_replay_slowdown with two constants changed; same lever.
BOOL __stdcall bf_replay_speedup(int)
{
  if (gPlayback != 0 && IsGameHost())
  {
    if ((gOptions == 6 && sgdwSpeedMul_g >= 0x10) || sgbReplayPaused_g != 0)
      return -1;
    return TRUE;
  }
  return FALSE;
}

// 0x480FB0, 130 B. The per-frame replay status tick: recompute the elapsed
// seconds (replay_dlg_elapsed_seconds's expression, unsigned), blink the PAUSED overlay every
// 600 ms, and ask the status bar to repaint if either moved. The 600 ms test is
// signed, so the stored tick is an int, as dlg_dlg.cpp views GetTickCount.
void sub_480FB0()
{
  int bProgressMoved = 0;
  DWORD dwProgress = sgReplayTicks_g[oc_bReplayInitialSpeed] *
                     oc_dwReplayFrame / 1000u;
  if (dwProgress != oc_dwReplayProgressShown)
  {
    oc_dwReplayProgressShown = dwProgress;
    bProgressMoved = 1;
  }

  int bBlinked = 0;
  if (sgbReplayPaused_g != 0)
  {
    int nNow = static_cast<int>(GetTickCount());
    if (nNow - oc_nReplayBlinkTick > 600)
    {
      oc_nReplayBlinkTick = nNow;
      bBlinked = 1;
      sgbReplayShowPaused_g = (sgbReplayShowPaused_g == 0);
    }
  }

  if (bProgressMoved || bBlinked)
    status_set_update();
}

// 0x481040, 273 B. Applies a playback speed: publishes the three settings,
// rescales the seven turn-rate slots, and asks the game loop and the status bar
// to pick them up. dwMult is unsigned for the divides and signed for the one
// comparison, through an int view taken once at the top; written `dwMult >= 8`,
// the compare is unsigned and the code is not 1.17's.
void __fastcall replay_set_speed(DWORD dwSpeed, DWORD dwMult, BYTE bPaused)
{
  int nMult = static_cast<int>(dwMult);

  gOptions = dwSpeed;
  sgbReplayPaused_g = bPaused;
  sgdwSpeedMul_g = nMult;

  if (dwSpeed == 0)
  {
    sgdwUpdatesPerFrame_g = 1;
    oc_ReplayTurnRates[0] = 0xA7 * nMult;
    oc_ReplayTurnRates[1] = 0x6F * nMult;
    oc_ReplayTurnRates[2] = 0x53 * nMult;
    oc_ReplayTurnRates[3] = 0x43 * nMult;
    oc_ReplayTurnRates[4] = 0x38 * nMult;
    oc_ReplayTurnRates[5] = 0x30 * nMult;
    oc_ReplayTurnRates[6] = 0x2A * nMult;
  }
  else
  {
    oc_ReplayTurnRates[0] = 0xA7u / nMult;
    oc_ReplayTurnRates[1] = 0x6Fu / nMult;
    oc_ReplayTurnRates[2] = 0x53u / nMult;
    oc_ReplayTurnRates[3] = 0x43u / nMult;
    oc_ReplayTurnRates[4] = 0x38u / nMult;
    oc_ReplayTurnRates[5] = 0x30u / nMult;
    oc_ReplayTurnRates[6] = 0x2Au / nMult;
    sgdwUpdatesPerFrame_g = (nMult >= 8) ? 10 : 1;
  }

  sub_445110(oc_ReplayTurnRates);
  status_set_update();
}

// The replay header and slot data helpers.

// 0x480540, 29 B. A 13-byte block copy, 8, 4 and 1 bytes, as 1.17 copies it.
void sub_480540(void *pOut)
{
  *static_cast<replay_seed_t *>(pOut) = oc_ReplaySeedBlob;
}

// 0x480560, 70 B. fastcall(ECX = header out, EDX = slots out).
void replay_assign_gamedata(void *pHeaderOut, void *pSlotsOut)
{
  replay_slot_t *pDst = static_cast<replay_slot_t *>(pSlotsOut);
  int i;

  memcpy(pHeaderOut, &sgReplayHeader_g, 0x8D);
  for (i = 0; i < 12; ++i)
    pDst[i] = sgReplayHeader_g.slots[i];
}

// 0x4805D0, 7 B. A 16-bit load into a 16-bit return.
WORD GetReplayMission()
{
  return oc_wReplayHdrA5;
}

// 0x4805F0, 53 B. Twelve 36-byte slot records out.
void replay_assign_player_slots(void *pOut)
{
  replay_slot_t *pDst = static_cast<replay_slot_t *>(pOut);
  int i;

  for (i = 0; i < 12; ++i)
    pDst[i] = sgReplayHeader_g.slots[i];
}

// 0x480670, 6 B.
int replay_get_total_frames()
{
  return static_cast<int>(oc_dwReplayTotalFrames);
}

// 0x4807F0, 29 B. The inverse of sub_480540.
void sub_4807F0(void *pIn)
{
  oc_ReplaySeedBlob = *static_cast<const replay_seed_t *>(pIn);
}

// 0x480810, 139 B. The whole replay header in: the 141-byte blob, the twelve
// slot records, then the eight-dword slot map. `ret 4h`: fastcall with the third
// argument on the stack.
void replay_set_gamedata(const void *pHeaderIn, const void *pSlotsIn,
                const DWORD *pSlotMap)
{
  const replay_slot_t *pSrc = static_cast<const replay_slot_t *>(pSlotsIn);
  int i;

  memcpy(&sgReplayHeader_g, pHeaderIn, 0x8D);
  for (i = 0; i < 12; ++i)
    sgReplayHeader_g.slots[i] = pSrc[i];

  sgReplaySlotMap_g[0] = pSlotMap[0];
  sgReplaySlotMap_g[1] = pSlotMap[1];
  sgReplaySlotMap_g[2] = pSlotMap[2];
  sgReplaySlotMap_g[3] = pSlotMap[3];
  sgReplaySlotMap_g[4] = pSlotMap[4];
  sgReplaySlotMap_g[5] = pSlotMap[5];
  sgReplaySlotMap_g[6] = pSlotMap[6];
  sgReplaySlotMap_g[7] = pSlotMap[7];
}

// 0x4808D0, 44 B. BOOL, not void, though all six callers ignore it: the freeing
// path returns 1 and the false return is the pointer test's fallthrough, 1.17's
// shape. 0x457 (1111) is 1.17's __LINE__.
BOOL map_free()
{
  if (sgpMap == NULL)
    return FALSE;

  SMemFree(sgpMap, "replay.cpp", 1111, 0);
  sgpMap = NULL;
  return TRUE;
}

// 0x480BD0, 13 B. fastcall(ECX = buffer, EDX = capacity) forwarding to the
// 3-argument __stdcall Storm import with a literal 0.
void sub_480BD0(char *pszBuffer, DWORD cchBuffer)
{
  *pszBuffer = '\0';
  SFileGetUserDataPath(pszBuffer, cchBuffer, 0);
}

// 0x4803D0, 354 B. Enumerates <userdata>\maps\replays\*.rep and hands each
// basename to the callback. Its strings are 1.17's ("\", "maps\replays\", "*.",
// "rep", "LastReplay"), with doubled backslashes below: `\r` is a real escape.
// CreateDirectoriesRecursive at 0x4CC4C0 is a body, not a thunk.

void sub_4803D0(void(__stdcall *pfnAddItem)(const char *))
{
  char szPath[0x104];
  WIN32_FIND_DATAA fd;
  HANDLE hFind;
  char *pDot;

  szPath[0] = '\0';
  SFileGetUserDataPath(szPath, 0x104, 0);
  SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "maps\\replays\\", 0x104);

  if (!CreateDirectoriesRecursive(szPath))
    return;

  SStrPack(szPath, "*.", 0x104);
  SStrPack(szPath, "rep", 0x104);

  hFind = FindFirstFileA(szPath, &fd);
  if (hFind == INVALID_HANDLE_VALUE)
    return;

  do
  {
    // 0x16, the hidden, system and directory bits, tested as a BYTE against the
    // low byte of dwFileAttributes.
    if ((fd.dwFileAttributes & 0x16) == 0)
    {
      pDot = strrchr(fd.cFileName, '.');
      if (pDot != NULL)
        *pDot = '\0';

      if (strlen(fd.cFileName) <= 0x1B &&
          strcmp(fd.cFileName, "LastReplay") != 0)
        pfnAddItem(fd.cFileName);
    }
  } while (FindNextFileA(hFind, &fd));

  FindClose(hFind);
}

// Callees for LoadReplayData no header declares. pfopen takes the file name in
// ECX.
void *__fastcall pfopen(const char *pszName, short sMode);
int __fastcall pfclose(void *pFile);

// The replay header blob: LoadReplayData decompresses exactly 633 bytes into
// 0x67CCA0, up to byte_67CF19, so every address the code reaches in
// 0x67CCA0..0x67CF19 (0x67CCA1, 0x67CCA5, 0x67CCA8, 0x67CCB4, 0x67CD41,
// 0x67CEF1, 0x67CF11 and the rest) is a field of it, spelled here by its use.
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_bReplayHdr0 (*reinterpret_cast<BYTE *>(OC_VA(0x67CCA0u)))
#else
extern BYTE oc_bReplayHdr0;
#endif

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_wReplayHdrA5 (*reinterpret_cast<WORD *>(OC_VA(0x67CCA5u)))
#else
extern WORD oc_wReplayHdrA5;
#endif

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_bReplayHdrE6 (*reinterpret_cast<BYTE *>(OC_VA(0x67CCE6u)))
#else
extern BYTE oc_bReplayHdrE6;
#endif

#ifdef OC_DLL_MODE
#define oc_wGameTypeMirror (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD oc_wGameTypeMirror;
#endif

// The same 633-byte blob as one object, for write_replay: its field stores must
// stay ahead of the whole-blob copy, and with separate names the compiler may
// sink them below it.
#pragma pack(push, 1)
struct REPLAY_HDR_BLOB {
  /*0x000*/ BYTE bIsExpansion;
  /*0x001*/ BYTE _unmapped_001[4];
  /*0x005*/ WORD wGameType;
  /*0x007*/ BYTE _unmapped_007[0x3F];
  /*0x046*/ BYTE bValid;
  /*0x047*/ BYTE _unmapped_047[0x232];
};
#pragma pack(pop)
static_assert(offsetof(REPLAY_HDR_BLOB, bIsExpansion) == 0x000, "write_replay mov [67CCA0h], al");
static_assert(offsetof(REPLAY_HDR_BLOB, wGameType) == 0x005, "write_replay mov [67CCA5h], ax");
static_assert(offsetof(REPLAY_HDR_BLOB, bValid) == 0x046, "write_replay mov byte [67CCE6h], 1");
static_assert(sizeof(REPLAY_HDR_BLOB) == 0x279, "LoadReplayData decompress(67CCA0h, 279h)");

#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define oc_ReplayHdrBlob (*reinterpret_cast<REPLAY_HDR_BLOB *>(OC_VA(0x67CCA0u)))
#else
extern REPLAY_HDR_BLOB oc_ReplayHdrBlob;
#endif

// sglBytes / sgpMap -- sglBytes is the byte count decompressed into it, then
// SMemAlloc's size; sgpMap holds that allocation.
#ifdef OC_DLL_MODE
#define sglBytes (*reinterpret_cast<DWORD *>(OC_VA(0x67CC74u)))
#else
extern DWORD sglBytes;
#endif

#ifdef OC_DLL_MODE
#define sgpMap (*reinterpret_cast<void **>(OC_VA(0x67CC78u)))
#else
extern void *sgpMap;
#endif

// gbExpansion_g's address, 0x536720, spelled here as a BYTE of this TU's own:
// the sources name it three ways.
#ifdef OC_DLL_MODE
#define oc_b536720 (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE oc_b536720;
#endif

// 0x480690, 342 B. Opens a replay file, checks its "reRS" magic (0x53526572,
// compared as a dword), reads the 633-byte header and loads the map blob behind
// it.
int LoadReplayData(const char *pszName, int *pbIsExpansion)
{
  DWORD dwMagic;
  int nOk;
  void *pFile;

  if (pbIsExpansion != NULL)
    *pbIsExpansion = 0;

  pFile = pfopen(pszName, 1);
  if (pFile == NULL)
    return 0;

  dwMagic = 0;
  nOk = decompress(&dwMagic, 4, pFile);
  if (nOk == 0)
    goto done;

  // The guards nest and a forward goto reaches the success path, which keeps the
  // failure block inline before it, as 1.17 lays it out.
  if (dwMagic != 0x53526572 && dwMagic != REPLAY_MAGIC_HX)  // Hyperion: and Hyperion's
  {
    nOk = 0;
    goto done;
  }

  nOk = decompress(&oc_bReplayHdr0, 0x279, pFile);
  if (nOk == 0)
    goto done;

  if (oc_bReplayHdrE6 == 0)
  {
    nOk = 0;
    goto done;
  }

  if (pbIsExpansion != NULL)
  {
    if (oc_bReplayHdr0 == 0)
      goto ok;
    *pbIsExpansion = 1;
  }

  // The same byte is read a second time: the store to *pbIsExpansion above may
  // alias it.
  if (oc_bReplayHdr0 != 0 && oc_b536720 == 0)
  {
    nOk = 0;
    goto done;
  }

ok:
  oc_wGameTypeMirror = oc_wReplayHdrA5;
  net_record_init();
  nOk = sub_478970(pFile);
  if (nOk == 0)
    goto done;

  nOk = decompress(&sglBytes, 4, pFile);
  if (nOk == 0)
    goto done;

  if (sgpMap != NULL)
    SMemFree(sgpMap, "replay.cpp", 0x440, 0);

  {
    // sglBytes is read before sgpMap is stored, 1.17's order, which the
    // compiler cannot reorder when both are literal addresses.
    void *pMap = SMemAlloc(sglBytes, "replay.cpp", 0x441, 0);
    DWORD dwBytes = sglBytes;
    sgpMap = pMap;
    nOk = decompress(pMap, dwBytes, pFile);
  }
  // Hyperion: a Hyperion replay's sections, which must be this build's rules and
  // data; a 1.17 replay carries none
  if (nOk != 0) {
    if (dwMagic == REPLAY_MAGIC_HX)
      nOk = ReplayHxRead(pFile);
    else
      ReplayHxForget();
  }
  if (nOk != 0)
    goto done;

  // The zero result goes straight into SMemFree's flags argument, as in 1.17
  // (saveload.h notes the same quirk).
  SMemFree(sgpMap, "replay.cpp", 0x445, nOk);
  sgpMap = NULL;

done:
  pfclose(pFile);
  return nOk;
}

// The /replay chat commands and the replay file operations.

// 0x47FD60, 168 B. "/replay show <what>": find <what> in rep_lb_types_g, build
// the leaderboard action block and post it. An index loop, signed, not a
// pointer walk.
BOOL replay_lb_show(char *pszWhat)
{
  for (int i = 0; i < 12; ++i)
  {
    if (strcmp(pszWhat, rep_lb_types_g[i].szName) == 0)
    {
      TrigAction lb = { 0 };
      lb.bAction = rep_lb_types_g[i].bAction;
      lb.wUnitType = rep_lb_types_g[i].wParam;
      act_leaderboard(&lb);
      statlb_refresh(0);
      return TRUE;
    }
  }
  return FALSE;
}

// 0x47FE10, 71 B. "/replay hide": the same block, all zero. `lb.bAction = 0;`
// is not dead: the `= { 0 }` fill covers that byte with a dword store, and 1.17
// makes both stores.
BOOL replay_lb_hide()
{
  TrigAction lb = { 0 };
  lb.bAction = 0;
  act_leaderboard(&lb);
  statlb_refresh(1);
  return TRUE;
}

// 0x47FFC0, 184 B. Deletes <userdata>\maps\replays\<name>.rep. The strings are
// 1.17's, with every backslash escaped: `\r` is a carriage return and a trailing
// backslash a line splice.
BOOL delete_replay(char *pszName)
{
  char szPath[0x104];

  szPath[0] = '\0';
  SFileGetUserDataPath(szPath, 0x104, 0);
  SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "maps\\replays\\", 0x104);
  if (CreateDirectoriesRecursive(szPath))
  {
    SStrPack(szPath, pszName, 0x104);
    SStrPack(szPath, ".", 0x104);
    SStrPack(szPath, "rep", 0x104);
    return pfdelete(szPath, FALSE);
  }
  return FALSE;
}

// 0x4802C0, 260 B. Save the current LastReplay under <name>, asking first if
// that file already exists. The GetLastError() call is unconditional and its
// result unused, as in 1.17.
int sub_4802C0(char *pszName)
{
  char szPath[0x104];
  DWORD dwAttr;

  if (SStrCmpI(pszName, "LastReplay", 0x7FFFFFFF) == 0)
    return 1;

  szPath[0] = '\0';
  SFileGetUserDataPath(szPath, 0x104, 0);
  SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "maps\\replays\\", 0x104);
  if (CreateDirectoriesRecursive(szPath))
  {
    SStrPack(szPath, pszName, 0x104);
    SStrPack(szPath, ".", 0x104);
    SStrPack(szPath, "rep", 0x104);

    dwAttr = GetFileAttributesA(szPath);
    GetLastError();
    if (dwAttr != 0xFFFFFFFFu && ask_replay_dlg_arg(pszName, 3) == 0)
      return -1;
    return copy_lastreplay_to(pszName);
  }
  return 0;
}

// 0x480BE0, 149 B. The "/replay " chat-command dispatcher; the literal's
// trailing space is part of it (8 characters). Exactly one space after the
// command name is consumed, and any other non-NUL character fails.
int parse_replay_command(char *pszText)
{
  if (pszText == NULL)
    return 0;
  if (strncmp(pszText, "/replay ", 8) != 0)
    return 0;

  char *pszArgs = pszText + 8;
  for (int i = 0; i < 2; ++i)
  {
    size_t nLen = strlen(rep_cmds_g[i].szName);
    if (strncmp(pszArgs, rep_cmds_g[i].szName, nLen) == 0)
    {
      pszArgs += nLen;
      if (*pszArgs != '\0')
      {
        if (*pszArgs != ' ')
          return 0;
        ++pszArgs;
      }
      return rep_cmds_g[i].pfn(pszArgs);
    }
  }
  return 0;
}

// The replay dialog's lifecycle.

// Its globals, dual-spelled, one #ifndef per name; addresses no source names
// are spelled `oc_*`.

// dword_67CC94 -- the loaded Load Replay dialog, which do_replay_dlg stores and
// passes to dlg_run_modal.
#ifdef OC_DLL_MODE
#define oc_pReplayDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x67CC94u)))
#else
extern ctrl_t *oc_pReplayDlg;
#endif

// dword_67CC98 -- the glue background tile path for the current race skin,
// off_508E3C[race + (flag ? 3 : 0)], which replay_dlg_init loads and both
// message boxes pass.
#ifdef OC_DLL_MODE
#define oc_pszReplayGlueTile (*reinterpret_cast<const char **>(OC_VA(0x67CC98u)))
#else
extern const char *oc_pszReplayGlueTile;
#endif

// off_508E3C -- six pointers to the per-race glue tile GRPs,
// "glue\palzd\tile.grp" to "glue\palpv\tile.grp"; do_replay_dlg indexes 0..5.
#ifdef OC_DLL_MODE
#define oc_ReplayGlueTiles (reinterpret_cast<const char **>(OC_VA(0x508E3Cu)))
#else
extern const char *oc_ReplayGlueTiles[6] OC_UNALIGNED;
#endif

// sReplayDlgEvtFcnTbl_g -- the dialog's per-control event-function table:
// replaydlg_listbox_fcn, replaydlg_edit_name_fcn, replaydlg_btn_delete_fcn,
// NULL. replay_dlg_init passes its 0x10 bytes to glue_init_screen_dlg.
#ifdef OC_DLL_MODE
#define sReplayDlgEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508E54u)))
#else
extern PFN_CTRL_EVT sReplayDlgEvtFcnTbl_g[4] OC_UNALIGNED;
#endif

// byte_67CF19 -- the remembered list box selection, a BYTE, which
// replay_dlg_deactivate stores and replay_dlg_activate restores.
#ifdef OC_DLL_MODE
#define oc_bReplayDlgSel (*reinterpret_cast<BYTE *>(OC_VA(0x67CF19u)))
#else
extern BYTE oc_bReplayDlgSel;
#endif

// byte_67CF1C (oc_szReplayName) is dual-spelled once, in the globals block
// above; replaydlg_edit_evn_char limits it to 27 characters.

// dword_67CF38 / dword_67CF3C -- do_replay_dlg's two arguments, published for
// the dialog callbacks.
#ifdef OC_DLL_MODE
#define oc_dwReplayDlgFlag (*reinterpret_cast<int *>(OC_VA(0x67CF38u)))
#else
extern int oc_dwReplayDlgFlag;
#endif

#ifdef OC_DLL_MODE
#define oc_dwReplayDlgRace (*reinterpret_cast<int *>(OC_VA(0x67CF3Cu)))
#else
extern int oc_dwReplayDlgRace;
#endif

// dword_665F58 / dword_665F5C -- event_Win.cpp's, two DWORDs no source names.
#ifdef OC_DLL_MODE
#define oc_dwEventWin665F58 (*reinterpret_cast<int *>(OC_VA(0x665F58u)))
#else
extern int oc_dwEventWin665F58;
#endif

#ifdef OC_DLL_MODE
#define oc_dwEventWin665F5C (*reinterpret_cast<int *>(OC_VA(0x665F5Cu)))
#else
extern int oc_dwEventWin665F5C;
#endif

// Callees no other header declares.

// 0x46C870 / 0x46C840. EDX is a 16-bit control index and one stack dword follows
// (`ret 4h`); ECX is only passed on.
void __fastcall set_size_escape(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
void __fastcall set_size_enter(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);


// dfl_load_bmp (0x47BB70) is declared in src117/h/opt_dlgs.h.

// edit_get_text and edit_ctrl_edit are declared in src117/h/save_dlg.h;
// edit_ctrl_edit returns int, which replaydlg_edit_name_fcn passes back.



// modeless_okcancel_dlg and modeless_ok_dlg are declared in okcancel.h,
// included above.

// init_lang_imm (0x43F3F0) is declared in src117/h/event_Win.h, included above.

// replay_dlg_init's closing chain: opt_get_tile_grp and sub_47C110 are declared
// in src117/h/opt_dlgs.h, sub_4CA820 in src117/h/dlg_effects.h.

// alloc_load_bmp (0x444930), do_replay_dlg's dlg_load measurement callback, is
// declared in src117/h/glues.h.

// load_game_data -- 0x444890 (gamedata.cpp), spelled as src117/lang/gametype.cpp
// spells it.
void *load_game_data(const char *pszFile, void *pBuffer,
                                DWORD *pdwSize, const char *pszTag, int nLine);

// The replay writer and the replay dialog's bodies, in oracle VA order.

// 0x480080, 564 B. Writes the finished game's replay under <name>: the "reRS"
// magic, the 633-byte header, the command record, then the map CHK behind its
// size. A failed write deletes the partial file. dwWord is both the magic and
// get_map_chk's size out-parameter, one stack slot as in 1.17.
int write_replay(const char *pszName, BOOL bOverwrite)
{
  REPLAY_HDR_BLOB hdr;
  char szMsg[0x100];
  char szPath[0x104];
  DWORD dwWord;
  DWORD dwSize;
  void *pFile;
  void *pChk;
  int nOk;

  szPath[0] = '\0';
  SFileGetUserDataPath(szPath, 0x104, 0);
  SStrPack(szPath, "\\", 0x104);
  SStrPack(szPath, "maps\\replays\\", 0x104);
  if (CreateDirectoriesRecursive(szPath)) {
    SStrPack(szPath, pszName, 0x104);
    SStrPack(szPath, ".", 0x104);
    SStrPack(szPath, "rep", 0x104);

    if (bOverwrite && !pfdelete(szPath, FALSE)) {
      DWORD dwAttr = GetFileAttributesA(szPath);
      GetLastError();
      if (dwAttr != 0xFFFFFFFFu) {
        psnprintf(szMsg, 0x100, error_get_str(2), oc_szReplayName);
        modeless_ok_dlg(szMsg, 1, oc_pszReplayGlueTile);
        return -1;
      }
    }

    pFile = pfopen(szPath, 0x17);
    if (pFile == NULL)
      return 0;

    oc_ReplayHdrBlob.bIsExpansion = oc_b536720;
    oc_ReplayHdrBlob.bValid = 1;
    oc_ReplayHdrBlob.wGameType = oc_wGameTypeMirror;
    hdr = oc_ReplayHdrBlob;
    // Hyperion: a benchmark's replay, every slot a computer (port_bench.cpp)
    if (PortBenchMaking())
      PortBenchPatchHeader(&hdr, sizeof hdr);
    else if (PortWatchMaking())
      PortWatchPatchHeader(&hdr, sizeof hdr);  // and a watched skirmish's
    // Hyperion: a game past 1.17's limits writes Hyperion's replay
    dwWord = ReplayHxNoted() ? REPLAY_MAGIC_HX : 0x53526572;
    nOk = compress(&dwWord, 4, pFile);
    if (nOk == 0)
      goto done;
    nOk = compress(&hdr, sizeof(hdr), pFile);
    if (nOk == 0)
      goto done;
    if (PortBenchMaking() || PortWatchMaking()) {
      dwWord = 0;  // and no commands
      nOk = compress(&dwWord, 4, pFile);
    } else {
      nOk = net_record_write(reinterpret_cast<intptr_t>(pFile));
    }
    if (nOk == 0)
      goto done;
    pChk = get_map_chk(static_cast<const char *>(GetCurrentMap(1)), &dwWord);
    if (pChk == NULL) {
      nOk = 0;
      goto done;
    }
    dwSize = dwWord;
    nOk = compress(&dwSize, 4, pFile);
    if (nOk == 0) {
      mem_free(pChk, "replay.cpp", 0x3B1);
    } else {
      nOk = compress(pChk, dwWord, pFile);
      mem_free(pChk, "replay.cpp", 0x3B8);
      if (nOk != 0 && ReplayHxNoted())
        nOk = ReplayHxWrite(pFile);  // Hyperion: Hyperion's sections
    }

done:
    pfclose(pFile);
    if (nOk == 0)
      pfdelete(szPath, FALSE);
    return nOk != 0;
  }
  return 0;
}

// 0x480900, 204 B. The "..." is a strcat of a 4-byte literal, 1.17's .rdata at
// 0x4ECDEC.
// malloc and free come from the same CRT: the block is freed before the dialog
// opens.
int ask_replay_dlg_arg(char *pszName, int nErrId)
{
  char szMsg[0x100];
  char *pszShort;
  int nFit;

  pszShort = static_cast<char *>(malloc(strlen(pszName) + 3));
  font_set(sgpFonts_g[2]);
  nFit = string_width(pszName, 0xE6);
  font_set(NULL);
  strncpy(pszShort, pszName, nFit);
  pszShort[nFit] = '\0';
  if (nFit != static_cast<int>(strlen(pszName)))
    strcat(pszShort, "...");
  psnprintf(szMsg, 0x100, error_get_str(nErrId), pszShort);
  free(pszShort);
  return modeless_okcancel_dlg(szMsg, 1, oc_pszReplayGlueTile) == -2;
}

// 0x4809D0, 231 B. The tile index is `nArg0 + (nArg1 ? 3 : 0)`. The three exits
// are written alike (`glue = NULL; return nRet;`), so that they share the
// epilogue.
int do_replay_dlg(int nArg0, int nArg1)
{
  int nRet = 0;
  int nResult;
  int nRc;

  oc_dwReplayDlgFlag = nArg1;
  oc_dwReplayDlgRace = nArg0;
  oc_bReplayDlgSel = 0;
  oc_pszReplayGlueTile = oc_ReplayGlueTiles[nArg0 + (nArg1 ? 3 : 0)];
  oc_pReplayDlg = dlg_load("rez\\savegame.bin",
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           "replay.cpp", 617);
  init_lang_imm(1);
  nResult = dlg_run_modal(oc_pReplayDlg, 0, replay_dlg_fcn);
  init_lang_imm(0);
  if (nResult != -3)
  {
    switch (nResult)
    {
    case -2:
      nRc = sub_4802C0(oc_szReplayName);
      if (nRc == 0)
        modeless_ok_dlg(error_get_str(nRc + 0x65), 1, oc_pszReplayGlueTile);
      else if (nRc == 1)
        nRet = 1;
      break;
    case 3:
      delete_replay(oc_szReplayName);
      break;
    }
  }
  oc_pszReplayGlueTile = NULL;
  return nRet;
}

// 0x480AC0, 265 B. The tail, from `pLbox = ctrl_find_ndx(pCtrl, 1)` on, is
// gray_delete_btn (0x49B360) written out against pLbox.
void replaydlg_edit_evn_char(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  ctrl_t *pDel;
  ctrl_t *pLbox;
  ctrl_t *pBtn;
  char *pszText;
  const char *pszReject;

  // error_get_str is its own statement, so that it is called before the key is
  // loaded, as in 1.17.
  pszReject = error_get_str(0x11);
  if (strchr(pszReject, *reinterpret_cast<WORD *>(pEvent->_unmapped08)) != NULL)
    return;

  if (*reinterpret_cast<WORD *>(pEvent->_unmapped08) != 8)
  {
    pszText = static_cast<char *>(pCtrl->pData);
    if (strlen(pszText) >= 0x1B)
    {
      if (oc_dwEventWin665F58 == 1)
        oc_dwEventWin665F5C = 1;
    }
    else if (*reinterpret_cast<WORD *>(pEvent->_unmapped08) != 0x20 ||
             *pszText != '\0')
    {
      edit_ctrl_edit(pCtrl, pEvent);
    }
  }
  else
  {
    edit_ctrl_edit(pCtrl, pEvent);
  }

  pDel = ctrl_find_ndx(pCtrl, -2);
  if (*static_cast<char *>(pCtrl->pData) != '\0')
    ctrl_ungray(pDel);
  else
    ctrl_gray(pDel);

  pLbox = ctrl_find_ndx(pCtrl, 1);
  if (lbox_get_selection(reinterpret_cast<lbox_t *>(pLbox)) != 0xFF)
  {
    ctrl_set_val(pLbox, 0xFF);
    ctrl_set_update(pLbox);
    // gray_delete_btn(pLbox), written out.
    pBtn = ctrl_find_ndx(pLbox, 3);
    if (pBtn != NULL)
    {
      if (lbox_get_count(reinterpret_cast<lbox_t *>(pLbox)) != 0 &&
          lbox_get_selection(reinterpret_cast<lbox_t *>(pLbox)) != 0xFF)
        ctrl_ungray(pBtn);
      else
        ctrl_gray(pBtn);
    }
  }
}

// 0x480C80, 124 B. Ends in a tail call to ctrl_set_text. The
// ctrl_find_ndx(pDlg, 4) result is a statement of its own, so that it is called
// before glues_get_str, as in 1.17.
void replay_dlg_activate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;

  glue_screen_activate(pDlg);
  set_size_escape(pDlg, 5, 0x20000000);
  set_size_enter(pDlg, 3, 0x20000000);
  pCtrl = ctrl_find_ndx(pDlg, 3);
  if (pCtrl != NULL)
    ctrl_hide(pCtrl);
  lbox_set_selection(reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 1)),
                     oc_bReplayDlgSel);
  pCtrl = ctrl_find_ndx(pDlg, 4);
  ctrl_set_text(pCtrl, glues_get_str(0xB3));
}

// 0x480D00, 107 B. Ends in a tail call to glues_dlg_deactivate. strip_trailing
// (0x4814B0) is written out here, with a separate `--nAt;` as in its own body.
void replay_dlg_deactivate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;
  int nAt;

  oc_bReplayDlgSel =
      lbox_get_selection(reinterpret_cast<lbox_t *>(ctrl_find_ndx(pDlg, 1)));
  pCtrl = ctrl_find_ndx(pDlg, 2);
  if (pCtrl != NULL)
  {
    edit_get_text(pCtrl, oc_szReplayName);
    nAt = static_cast<int>(strlen(oc_szReplayName));
    --nAt;
    while (nAt >= 0)
    {
      if (oc_szReplayName[nAt] != ' ')
        break;
      oc_szReplayName[nAt] = '\0';
      --nAt;
    }
  }
  glues_dlg_deactivate(pDlg);
}

// 0x480DB0, 75 B. The closing chain is one nested expression,
// sub_4CA820(0x50, sub_47C110(opt_get_tile_grp(), 4, 0, 0), 1), whose stack
// arguments 1.17 pushes up front.
void replay_dlg_init(ctrl_t *pDlg)
{
  glue_init_screen_dlg(pDlg, sReplayDlgEvtFcnTbl_g, 0x10, 0);
  dfl_load_bmp(pDlg, oc_pszReplayGlueTile);
  pDlg->dwFlags |= 0x2040000;
  sub_4CA820(0x50, sub_47C110(opt_get_tile_grp(), 4, 0, 0), 1);
}

// Hyperion: a LAN match's replay as it is played (port_lanfeed.cpp).
//
// replay_live_parts takes what write_replay would write but the commands, changing nothing:
// the header as write_replay sets it, the map's CHK (the caller frees it with mem_free), and,
// in a Hyperion game, its sections, written to pszHxPath; *pdwMagic says which replay it is.
// replay_write_parts writes a replay of such parts and a record, as write_replay writes one.
BOOL replay_live_parts(void *pHeader, void **ppChk, DWORD *pdwChk, DWORD *pdwMagic, const char *pszHxPath)
{
  REPLAY_HDR_BLOB hdr = oc_ReplayHdrBlob;
  hdr.bIsExpansion = oc_b536720;
  hdr.bValid = 1;
  hdr.wGameType = oc_wGameTypeMirror;
  memcpy(pHeader, &hdr, sizeof hdr);
  *pdwMagic = ReplayHxNoted() ? REPLAY_MAGIC_HX : 0x53526572;
  *ppChk = get_map_chk(static_cast<const char *>(GetCurrentMap(1)), pdwChk);
  if (*ppChk == NULL)
    return FALSE;
  if (!ReplayHxNoted())
    return TRUE;
  void *pFile = pfopen(pszHxPath, 0x17);
  if (pFile == NULL)
    return FALSE;
  const int nOk = ReplayHxWrite(pFile);
  pfclose(pFile);
  return nOk != 0;
}

void replay_live_free(void *pChk)
{
  mem_free(pChk, "replay.cpp", 0);
}

BOOL replay_write_parts(const char *pszPath, DWORD dwMagic, const void *pHeader, const void *pRecord, int nRecord,
                        const void *pChk, DWORD dwChk, const void *pHx, DWORD dwHx)
{
  void *pFile = pfopen(pszPath, 0x17);
  if (pFile == NULL)
    return FALSE;
  DWORD dwWord = dwMagic;
  int nLen = nRecord;
  DWORD dwSize = dwChk;
  const int nOk = compress(&dwWord, 4, pFile) && compress(pHeader, sizeof(REPLAY_HDR_BLOB), pFile) &&
                  compress(&nLen, 4, pFile) && (nRecord <= 0 || compress(pRecord, nRecord, pFile)) &&
                  compress(&dwSize, 4, pFile) && compress(pChk, static_cast<int>(dwChk), pFile) &&
                  (dwHx == 0 || pfwrite(pHx, static_cast<int>(dwHx), pFile));
  pfclose(pFile);
  if (!nOk)
    pfdelete(const_cast<char *>(pszPath), FALSE);
  return nOk != 0;
}
