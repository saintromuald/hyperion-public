// statcmd.cpp -- the command card: the action-button panel, its tooltips and
// hotkey dispatch, and the replay progress bar that replaces it in playback.
// Functions are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>       // offsetof, for the struct static_asserts
#include <ctype.h>        // Hyperion: isalnum, toupper (StatCmdHudTip)
#include <string.h>       // memset
#include <intrin.h>       // _ReadWriteBarrier -- sub_4A0DC0

#include "../h/platform.h"
#include "../h/statcmd.h"
#include "../h/rect16.h"
#include "../h/dialog.h"    // ctrl_t, dlgevt_t, PFN_CTRL_EVT/DRAW
#include "../h/dlg_ctrl.h"  // ctrl_find, ctrl_hide, ctrl_chainr, ...
#include "../h/dlg_dlg.h"   // dlg_deactivate
#include "../h/bmp.h"       // bmp_t, bmpsrc_t, bmp_copy_rect, bmp_destroy
#include "../h/minimap.h"   // ctxhelp_hide
#include "../h/order.h"     // status_get_str
#include "../h/victory.h"   // psnprintf
#include "../h/net_cmds.h"  // IsGamePaused
#include "../h/glues.h"     // mem_free, alloc_load_bmp
#include "../h/gamedata.h"  // ReadGroupFile
#include "../h/snd.h"       // load_game_data
#include "../h/statbtn.h"   // target_is_on, placebox_is_on
#include "../h/statdraw.h"  // statdraw_replay_progress
#include "../h/CtxtHelp.h"  // ctxhelp_get_surface, img_ctxhelp_invalid
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudHelpPlace, PortHudConsoleCursor
#include "../h/dlg_draw.h"  // font_set, font_set_color_tbl
#include "../h/gds_grptobmp.h"  // grp_to_bmp
#include "../h/gds_bmpmas.h"   // bmp_mask
#include "../h/grp.h"          // FRAME, GROUP
#include "../h/group.h"        // grh_get_header
#include "../h/statdata.h"     // grhsize_t
#include "../h/rect32.h"
#include "../port/data/port_moddata.h"  // Hyperion: ModConsoleGroup, a mod's icons
#include "../h/CUnitUpgrade.h"  // get_upgrade_ore_cost, get_upgrade_gas_cost
#include "../h/saveload.h"      // get_upgrade_level
#include "../gen/globals117.h"
#include "../port/gameplay/port_hotkeys.h"  // Hyperion: PortCardKey, PortCardTooltip
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX
#include "../port/render/port_console.h"  // Hyperion: PortConsoleRace

// The globals this TU reaches, dual-spelled: one guard per name, all below the
// generated header, so its spelling wins for a name it declares. gpCmdIcons
// @0x6AAA74 comes from src117/gen/globals117.h.

// 0x6AAAFC -- the "command card needs rebuilding" request flag. A DWORD despite
// its `sgb` prefix: every access is 32-bit.
#ifndef OC_SGBFORCESTATCMDUPDATE_G
#define OC_SGBFORCESTATCMDUPDATE_G
#ifdef OC_DLL_MODE
#define sgbForceStatCmdUpdate_g (*reinterpret_cast<DWORD *>(OC_VA(0x6AAAFCu)))
#else
extern DWORD sgbForceStatCmdUpdate_g;
#endif
#endif

// 0x6AAB04 / 0x6AAB14 -- the tooltip record, eight WORDs and the level WORD
// after them: sgwStatCmdTooltipData_g and sgwStatCmdTooltipLevel_g, from the
// generated header. Never take sizeof the array: its DLL spelling is a bare
// WORD *.

// 0x6AAA70 gpCmdBtns -- the command button group, a `void *` like gpCmdIcons
// beside it; this TU only frees it.
#ifndef OC_GPCMDBTNS
#define OC_GPCMDBTNS
#ifdef OC_DLL_MODE
#define gpCmdBtns (*reinterpret_cast<void **>(OC_VA(0x6AAA70u)))
#else
extern void *gpCmdBtns;
#endif
#endif

// 0x6AAA78 / 0x6AAA80 -- the replay progress bar's two bitmaps, empty and full
// (pbrempt.pcx and pbrfull.pcx; the names are ours). Each one's pPixels, at +4,
// is the loaded test.
#ifndef OC_SGBMPREPLPROGRESSBAREMPTY_G
#define OC_SGBMPREPLPROGRESSBAREMPTY_G
#ifdef OC_DLL_MODE
#define sgBmpReplProgressbarEmpty_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6AAA78u)))
#else
extern bmp_t sgBmpReplProgressbarEmpty_g;
#endif
#endif

#ifndef OC_SGBMPREPLPROGRESSBARFULL_G
#define OC_SGBMPREPLPROGRESSBARFULL_G
#ifdef OC_DLL_MODE
#define sgBmpReplProgressbarFull_g (*reinterpret_cast<bmp_t *>(OC_VA(0x6AAA80u)))
#else
extern bmp_t sgBmpReplProgressbarFull_g;
#endif
#endif

// 0x6AAAE8 -- the command-card dialog.
#ifndef OC_SGPSTATCMDDLG
#define OC_SGPSTATCMDDLG
#ifdef OC_DLL_MODE
#define sgpStatCmdDlg (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AAAE8u)))
#else
extern ctrl_t *sgpStatCmdDlg;
#endif
#endif

// 0x6AAAEC -- the control the pointer is currently over.
#ifndef OC_SGPSTATCMDHOVERCTRL_G
#define OC_SGPSTATCMDHOVERCTRL_G
#ifdef OC_DLL_MODE
#define sgpStatCmdHoverCtrl_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x6AAAECu)))
#else
extern ctrl_t *sgpStatCmdHoverCtrl_g;
#endif
#endif

// The two globals that build the replay progress bar's .pcx paths.

// 0x5264C2 -- a BYTE, used here only as the index into the table below.
#ifndef OC_BYTE_5264C2
#define OC_BYTE_5264C2
#ifdef OC_DLL_MODE
#define byte_5264C2 (*reinterpret_cast<BYTE *>(OC_VA(0x5264C2u)))
#else
extern BYTE byte_5264C2;
#endif
#endif

// 0x503358 gszRaceChars_g, gamedata.cpp's -- signed chars, each the path
// character that goes between "game\\" and a .pcx name. Its extent is not known,
// so it is declared incomplete and never sizeof'd.
#ifndef OC_GSZRACECHARS_G
#define OC_GSZRACECHARS_G
#ifdef OC_DLL_MODE
#define gszRaceChars_g (reinterpret_cast<char *>(OC_VA(0x503358u)))
#else
extern char gszRaceChars_g[];
#endif
#endif

// ctrl_t +0x24 and +0x26, the command button's payload, reached by cast since
// dialog.h leaves +0x24..+0x29 unmapped: +0x24 is a WORD, the icon (0xFFFF for
// none) or the progress bar's percent, and +0x26 the button record's pointer,
// unaligned, which ctrl_t's pack(1) allows.
#define ctrl_w24(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped24[0]))

// dlgevt_t +0x08 and +0x0A, the keyboard arm of the per-event payload dialog.h
// leaves unnamed: the virtual key and the modifier bits.
#define evt_vkey(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped08[0]))
#define evt_mods(p) (*reinterpret_cast<WORD *>(&(p)->_unmapped08[2]))

// The remaining globals.

// 0x6AAAF2 / 0x6AAAF3 -- the order ids the card highlights, the selected unit's
// and its subunit's, each an index into order_highlight[]; 0x6AAAF3 is 0xFF
// with no subunit.
#ifndef OC_BYTE_6AAAF2
#define OC_BYTE_6AAAF2
#ifdef OC_DLL_MODE
#define byte_6AAAF2 (*reinterpret_cast<BYTE *>(OC_VA(0x6AAAF2u)))
#else
extern order_id_t byte_6AAAF2;
#endif
#endif

#ifndef OC_BYTE_6AAAF3
#define OC_BYTE_6AAAF3
#ifdef OC_DLL_MODE
#define byte_6AAAF3 (*reinterpret_cast<BYTE *>(OC_VA(0x6AAAF3u)))
#else
extern order_id_t byte_6AAAF3;
#endif
#endif

// Hyperion: "no subunit" in byte_6AAAF3, 0xFF in 1.17's byte and every bit of the
// wider order id on x64, where 0xFF is an order a mod may have
static const order_id_t kNoSubOrder = static_cast<order_id_t>(-1);

// 0x6AAB00 -- the highlighted button's icon, 0xFFFF for none.
#ifndef OC_WORD_6AAB00
#define OC_WORD_6AAB00
#ifdef OC_DLL_MODE
#define word_6AAB00 (*reinterpret_cast<WORD *>(OC_VA(0x6AAB00u)))
#else
extern WORD word_6AAB00;
#endif
#endif

// 0x665E68 -- gKeyTbl[VK_SHIFT], a BYTE, handed to a button's action as its
// second argument.
#ifndef OC_BYTE_665E68
#define OC_BYTE_665E68
#ifdef OC_DLL_MODE
#define byte_665E68 (*reinterpret_cast<BYTE *>(OC_VA(0x665E68u)))
#else
extern BYTE byte_665E68;
#endif
#endif

// statcmd_btn_t and ctrl_p26 are in statcmd.h (Hyperion: the HUD's data layer reads the
// card's buttons too, port_hudstate.cpp)

// 0x6AAAF4 -- the command card's pending buttonset, a WORD: 0xE4 for none,
// 0xF4 to 0xF7 the shared sets sub_4A15A0 picks.
#ifndef OC_WORD_6AAAF4
#define OC_WORD_6AAAF4
#ifdef OC_DLL_MODE
#define word_6AAAF4 (*reinterpret_cast<WORD *>(OC_VA(0x6AAAF4u)))
#else
extern WORD word_6AAAF4;
#endif
#endif

// 0x50AE98 gCards -- one 12-byte record per card (buttonset): the button
// count, the button array, and a linked card. Its extent is not known.
struct statcmd_card_t {
  /*0x00*/ WORD wBtnCount;
  /*0x02*/ BYTE _unmapped02[2];
  /*0x04*/ statcmd_btn_t *pBtns;
  /*0x08*/ WORD wLink;
  /*0x0A*/ BYTE _unmapped0A[2];
};
OC_X86_LAYOUT(sizeof(statcmd_card_t) == 0x0C, "statcmd_card_t size        lea esi, [eax+eax*2] / [esi*4+...]");
static_assert(offsetof(statcmd_card_t, wBtnCount) == 0x00, "statcmd_card_t::wBtnCount  cmp di, word ptr [ecx*4+50AE98h]");
OC_X86_LAYOUT(offsetof(statcmd_card_t, pBtns)     == 0x04, "statcmd_card_t::pBtns      mov esi, dword ptr [esi*4+50AE9Ch]");
OC_X86_LAYOUT(offsetof(statcmd_card_t, wLink)     == 0x08, "statcmd_card_t::wLink      cmp word ptr [ecx+50AEA0h], si");

// Hyperion: a card by its id, a mod's unit's own too (defined at the end)
struct statcmd_card_t;
statcmd_card_t *StatcmdCard(WORD wCard);

#ifndef OC_GCARDS
#define OC_GCARDS
#ifdef OC_DLL_MODE
#define gCards (reinterpret_cast<statcmd_card_t *>(OC_VA(0x50AE98u)))
#else
extern statcmd_card_t gCards[];
#endif
#endif

// 0x6AAAF0 / 0x6AAAF8 -- the card the command card shows and the special card
// that overrides it (targeting, placement, replay), WORDs.
#ifndef OC_SGCARD
#define OC_SGCARD
#ifdef OC_DLL_MODE
#define sgCard (*reinterpret_cast<WORD *>(OC_VA(0x6AAAF0u)))
#else
extern WORD sgCard;
#endif
#endif

#ifndef OC_SGWSPECIALCARD
#define OC_SGWSPECIALCARD
#ifdef OC_DLL_MODE
#define sgwSpecialCard (*reinterpret_cast<WORD *>(OC_VA(0x6AAAF8u)))
#else
extern WORD sgwSpecialCard;
#endif
#endif

// 0x67CC7C -- replay paused, a DWORD.
#ifndef OC_SGBREPLAYPAUSED_G
#define OC_SGBREPLAYPAUSED_G
#if defined(OC_DLL_MODE) || defined(OC_PAYLOAD)
#define sgbReplayPaused_g (*reinterpret_cast<DWORD *>(OC_VA(0x67CC7Cu)))
#else
extern DWORD sgbReplayPaused_g;
#endif
#endif

// 0x50D638 -- the command card's per-control-type handler table, handed to
// ctrl_set_evt_fcns with a byte count of 0x28.
#ifndef OC_SSTATCMDEVTFCNTBL_G
#define OC_SSTATCMDEVTFCNTBL_G
#ifdef OC_DLL_MODE
#define sStatCmdEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x50D638u)))
#else
extern PFN_CTRL_EVT sStatCmdEvtFcnTbl_g[];
#endif
#endif

// 0x6AAA88 -- the buffer GameLoadImage fills from ticon.pcx; only its address
// is taken, and its extent is not known.
#ifndef OC_SGBMPTICON_G
#define OC_SGBMPTICON_G
#ifdef OC_DLL_MODE
#define sgBmpTIcon_g (reinterpret_cast<BYTE *>(OC_VA(0x6AAA88u)))
#else
extern BYTE sgBmpTIcon_g[] OC_UNALIGNED;
#endif
#endif

// 0x50D630 -- the icon frame shown beside each of the tooltip's eight numbers;
// 1 means "the race's own", replaced by byte_5264C2 + 1. Its extent is not known.
#ifndef OC_SGBTOOLTIPICON_G
#define OC_SGBTOOLTIPICON_G
#ifdef OC_DLL_MODE
#define sgbTooltipIcon_g (reinterpret_cast<BYTE *>(OC_VA(0x50D630u)))
#else
extern BYTE sgbTooltipIcon_g[];
#endif
#endif

// 0x6AB3F0 -- the command icons group, handed to grp_to_bmp in ECX. Spelled
// as src117/lang/statres.cpp spells it.
#ifndef OC_GPICONSGRP
#define OC_GPICONSGRP
#ifdef OC_DLL_MODE
#define gpIconsGrp (*reinterpret_cast<GROUP **>(OC_VA(0x6AB3F0u)))
#else
extern GROUP *gpIconsGrp;
#endif
#endif

// 0x6D3034 -- the font's line-extra byte, spelled as src117/lang/CtxtHelp.cpp
// spells it.
#ifndef OC_GBFONTLINEEXTRA
#define OC_GBFONTLINEEXTRA
#ifdef OC_DLL_MODE
#define gbFontLineExtra (*reinterpret_cast<BYTE *>(OC_VA(0x6D3034u)))
#else
extern BYTE gbFontLineExtra;
#endif
#endif

// 0x523E10 gubUnitFoodUsed and 0x524D8C upg_repeats -- per-type BYTE tables, at
// the extents count.cpp gives them.
#ifndef OC_GUBUNITFOODUSED
#define OC_GUBUNITFOODUSED
#ifdef OC_DLL_MODE
#define gubUnitFoodUsed (reinterpret_cast<BYTE *>(OC_VA(0x523E10u)))
#else
extern BYTE gubUnitFoodUsed[228] OC_UNALIGNED;
#endif
#endif

#ifndef OC_UPG_REPEATS
#define OC_UPG_REPEATS
#ifdef OC_DLL_MODE
#define upg_repeats (reinterpret_cast<BYTE *>(OC_VA(0x524D8Cu)))
#else
extern BYTE upg_repeats[64] OC_UNALIGNED;
#endif
#endif

// USER32!SetRect through the IAT slot 0x4EB630, spelled as
// src117/lang/minimap.cpp spells it.
extern "C" __declspec(dllimport)
int __stdcall SetRect(rect32_t *prc, int xLeft, int yTop, int xRight,
                      int yBottom);

// rle_draw_normal -- oracle 0x405E1E, spelled as src117/h/CImage.h declares it.
void __fastcall rle_draw_normal(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);

// font_adjust -- oracle 0x4CDF20, declared as src117/lang/CtxtHelp.cpp does.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// The bodies, in oracle VA order.

//
// The predicate ctrl_chainr walks the command card with: is the typed character
// this button's hotkey? The one call site passes a one-byte local through
// ctrl_chainr's dlgevt_t * slot, so the cast is there. nIndex is a signed short.
int statcmd_hotkey_cmp_fn(ctrl_t *pCtrl, const char *pch)
{
  if (pCtrl->nIndex <= 9 && pCtrl->nIndex >= 0 &&
      (pCtrl->dwFlags & 8) != 0 && (pCtrl->dwFlags & 2) == 0) {
    // Hyperion: the key as the profile sets it, by the button or its place (K1)
    const WORD wStr = ctrl_p26(pCtrl)->wStatusStrId;
    return *pch == PortCardKey(wStr, pCtrl->nIndex, status_get_str(wStr));
  }

  return 0;
}

//
// The command button's event handler: an outer switch over wTag 3 to 14 and,
// for 14, an inner switch over dwId 0, 2 and 4, a switch rather than an if
// ladder as 1.17 lowers it.
int statcmd_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  // Case 14 is written first, as 1.17 lays the arms out, so its break falls
  // through into the shared tail.
  switch (pEvent->wTag) {
    case 14:
      switch (pEvent->dwId) {
        case 0:
          pCtrl->pfnDraw = statcmd_btn_ctrl_draw_fn;
          ctrl_w24(pCtrl) = 0xFFFF;
          ctrl_hide(pCtrl);
          break;
        case 2:
          ctrl_p26(pCtrl)->pfnAction(ctrl_p26(pCtrl)->w0E, byte_665E68);
          return 1;
        case 4:
          if (pCtrl->dwFlags & 2)
            return 1;
          break;
      }
      break;

    case 3:
      statcmd_btn_ctrl_mouse_moved(pCtrl, pEvent);
      break;

    case 4:
    case 6:
      if (pCtrl->dwFlags & 2)
        return 0;
      break;

    case 5:
      reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl, pEvent);
      statcmd_dlg_mouse_moved(sgpStatCmdDlg, pEvent);
      return 1;
  }

  return reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl, pEvent);
}

// 0x4A0740 statcmd_btn_ctrl_mouse_moved (101 B). Tracks the hovered command
// button: clears the tooltip when the game is paused or the pointer left the
// control, otherwise starts hovering it. The two clearing tests are two arms
// with one body, as 1.17 lays them out; one `||` puts the clear after the hover.
void statcmd_btn_ctrl_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (IsGamePaused()) {
    sgpStatCmdHoverCtrl_g = NULL;
    statcmd_hide_tooltip();
  } else if (pCtrl != ctrl_find(pCtrl->pParent, pEvent)) {
    sgpStatCmdHoverCtrl_g = NULL;
    statcmd_hide_tooltip();
  } else if (pCtrl != sgpStatCmdHoverCtrl_g) {
    // pParent is read before the hover store, as the oracle orders the two.
    ctrl_t *pParent = pCtrl->pParent;
    sgpStatCmdHoverCtrl_g = pCtrl;
    statcmd_show_btn_tooltip(pParent, pCtrl);
  }
}

// Hyperion: the Attack key where the subgroup's card has no Attack, as StarCraft
// II's: in a game of Hyperion's selection with other kinds selected, a key none of the card's
// buttons takes that is the shared card's Attack key gives the selection that Attack, when one
// of its units could have it. Only on the unit's own card: not while targeting, placing or in a
// card's submenu, nor in a replay. The cards of a dropship or an overlord have Attack already,
// shown when a selected unit can attack; a High Templar's, a Defiler's or a Dark Archon's have
// none. The button is the shared card's for a mixed selection (0xF4), its key as the player's
// profile sets it (PortCardKey).
static int StatcmdSharedAttackKey(char ch)
{
  if (!SelectionMixed() || gPlayback || sgwSpecialCard != 0xE4 || gpStatUnit->bPlayer != g_LocalNationID)
    return 0;
  statcmd_card_t *pCard = StatcmdCard(0xF4);
  const statcmd_btn_t *pAttack = NULL;
  for (WORD i = 0; pAttack == NULL && i < pCard->wBtnCount; i++)
    if (pCard->pBtns[i].pfnAction == order_attack)
      pAttack = &pCard->pBtns[i];
  if (pAttack == NULL ||
      ch != PortCardKey(pAttack->wStatusStrId, pAttack->wMinIndex, status_get_str(pAttack->wStatusStrId)))
    return 0;
  for (int i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = StatUnits()[i];
    if (pUnit != NULL && pAttack->pfnCondition(pAttack->bConditionArg, static_cast<BYTE>(g_LocalNationID), pUnit) > 0) {
      order_attack(pAttack->w0E, byte_665E68);
      return 1;
    }
  }
  return 0;
}

// 0x4A07B0 statcmd_dlg_char_evn (85 B). The command card's WM_CHAR: lowercases
// the typed key, chains it down the card's children through
// statcmd_hotkey_cmp_fn, and sends event 3 to the hit.
int statcmd_dlg_char_evn(dlgevt_t *pEvent)
{
  char ch;

  // A set 0x100 suppresses the hotkey lookup.
  if ((evt_mods(pEvent) & 0x100) != 0)
    return 0;

  // Two locals keep the global's load ahead of the byte store, as in 1.17.
  {
    int c = tolower(evt_vkey(pEvent));
    ctrl_t *pDlg = sgpStatCmdDlg;
    ch = (char)c;

    if (ctrl_chainr(pDlg->pChildren,
                    reinterpret_cast<dlgevt_t *>(&ch),
                    reinterpret_cast<PFN_CTRL_EVT>(statcmd_hotkey_cmp_fn)) == 0)
      return StatcmdSharedAttackKey(ch);  // Hyperion: 0 in 1.17
  }

  return ctrl_send_msg(gpChainCtrl, 3, 0);
}

//
// Repoints the control's draw slot at the progress-bar renderer, zeroes its
// percentage, and loads each of the two bar bitmaps not yet loaded.
void statcmd_replay_progress_activate(ctrl_t *pCtrl)
{
  char szPath[0x104];

  pCtrl->pfnDraw = statcmd_replay_progress_draw;
  ctrl_w24(pCtrl) = 0;

  // Hyperion: the console's race, Console skin's (PortConsoleRace), here and below
  if (sgBmpReplProgressbarEmpty_g.pPixels == NULL) {
    psnprintf(szPath, sizeof(szPath), "%s%c%s",
              "game\\", gszRaceChars_g[PortConsoleRace()], "pbrempt.pcx");
    alloc_load_bmp(szPath, &sgBmpReplProgressbarEmpty_g, 0, "statcmd.cpp", 0x397);
  }
  if (sgBmpReplProgressbarFull_g.pPixels == NULL) {
    psnprintf(szPath, sizeof(szPath), "%s%c%s",
              "game\\", gszRaceChars_g[PortConsoleRace()], "pbrfull.pcx");
    alloc_load_bmp(szPath, &sgBmpReplProgressbarFull_g, 0, "statcmd.cpp", 0x39B);
  }
}

//
// Two teardowns, each guarded by its bitmap's pPixels. pCtrl is unused;
// statcmd_replay_progress_fcn's register order needs it.
void statcmd_replay_progress_deactivate(ctrl_t *pCtrl)
{
  if (sgBmpReplProgressbarEmpty_g.pPixels != NULL) {
    bmp_destroy(&sgBmpReplProgressbarEmpty_g);
    sgBmpReplProgressbarEmpty_g.pPixels = NULL;
  }
  if (sgBmpReplProgressbarFull_g.pPixels != NULL) {
    bmp_destroy(&sgBmpReplProgressbarFull_g);
    sgBmpReplProgressbarFull_g.pPixels = NULL;
  }
}

//
// A PFN_CTRL_DRAW, installed at +0x2E by the activate above: blits the whole
// empty bar over the control's rect, then the full bar clipped to the +0x24
// percent of the width.
void statcmd_replay_progress_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                 rect16_t *prcClip)
{
  bmpsrc_t src;

  src.rc.left = 0;
  src.rc.top  = 0;
  src.nWidth  = pCtrl->rc.right  - pCtrl->rc.left + 1;
  src.nHeight = pCtrl->rc.bottom - pCtrl->rc.top  + 1;
  src.rc.right  = src.rc.left + src.nWidth  - 1;
  src.rc.bottom = src.rc.top  + src.nHeight - 1;
  if (sgBmpReplProgressbarEmpty_g.pPixels != NULL)
    bmp_copy_rect(pCtrl->rc.left, pCtrl->rc.top,
                  &sgBmpReplProgressbarEmpty_g, &src);

  src.nWidth  = (pCtrl->rc.right - pCtrl->rc.left + 1) * ctrl_w24(pCtrl) / 100;
  src.nHeight = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  src.rc.right  = src.rc.left + src.nWidth  - 1;
  src.rc.bottom = src.rc.top  + src.nHeight - 1;
  if (sgBmpReplProgressbarFull_g.pPixels != NULL)
    bmp_copy_rect(pCtrl->rc.left, pCtrl->rc.top,
                  &sgBmpReplProgressbarFull_g, &src);
}

// 0x4A09F0 statcmd_replay_progress_fcn (54 B).
// The replay progress bar's control proc: activate or deactivate on event 0x0E,
// then chain to the default proc for the control type. deactivate is passed
// pCtrl too, which gives pCtrl ESI as in 1.17.
void statcmd_replay_progress_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:  statcmd_replay_progress_activate(pCtrl);   break;
      case 1:  statcmd_replay_progress_deactivate(pCtrl); break;
    }
  }

  reinterpret_cast<PFN_CTRL_EVT>(gCtrlEvtFcnTbl[pCtrl->wType])(pCtrl, pEvent);
}

// 0x4A0A30 DrawTooltipText (314 B).
// Render the command card's tooltip into the context-help surface: the text,
// then the cost line under it, then a frame sized to both and placed above the
// focused button, pulled left to stay inside the screen (640 pixels in 1.17,
// the view's width in the port).
// Hyperion: the HUD style's tooltip in place of the bubble: the
// focused button's own name with its key, then what 1.17 writes under it (the level of an
// upgrade) or, for a grey button, the reason it is grey; the cost line's figures, each with the
// icon 1.17 draws by it (sub_4A0DC0): the gas of the console's race, the supply and energy of
// the race the cost is counted in. A button that costs nothing has 0 for its cost's kind, the
// string's second byte, and its name after it (Stop, Set Rally Point, Cancel).
static bool StatCmdHudTip(const char *pszText)
{
  ctrl_t *pFocus = sgpStatCmdDlg != NULL ? sgpStatCmdDlg->pFocus : NULL;
  if (pFocus == NULL || pFocus->nIndex < 1 || pFocus->nIndex > 9 || ctrl_p26(pFocus) == NULL)
    return false;
  const statcmd_btn_t *pBtn = ctrl_p26(pFocus);
  const char *psz = status_get_str(pBtn->wStatusStrId);
  if (psz == NULL || psz[0] == '\0' || psz[2] == '\0')
    return false;
  const bool bGrey = (pFocus->dwFlags & 2) != 0;
  const char cKey = PortCardKey(pBtn->wStatusStrId, pFocus->nIndex, psz);
  char szKey[8] = "";
  if (cKey == 0x1B)
    strcpy(szKey, "Esc");
  else if (isalnum(static_cast<unsigned char>(cKey)))
    szKey[0] = static_cast<char>(toupper(static_cast<unsigned char>(cKey))), szKey[1] = '\0';
  const char *pszLines = bGrey ? pszText : strchr(pszText, '\n');
  if (pszLines != NULL && *pszLines == '\n')
    ++pszLines;
  const WORD *w = sgwStatCmdTooltipData_g;
  const int anCosts[4] = {w[0], w[1], w[5] + w[6] + w[7], w[2] + w[3] + w[4]};
  const int anRaces[4] = {0, byte_5264C2, w[5] != 0 ? 2 : w[6] != 0 ? 1 : 0, w[2] != 0 ? 2 : w[3] != 0 ? 1 : 0};
  return PortHudTipShow(pFocus->nIndex - 1, 0, 0, psz + 2, cKey == psz[0], szKey, pszLines, anCosts, anRaces);
}

// Hyperion: the live channel's hud_tip (port_control.cpp): the tooltip
// of the card's button at place nPlace (0 to 8) as the pointer resting on it shows it, or none
// for -1. False when no button is there.
bool StatCmdTipAt(int nPlace)
{
  if (sgpStatCmdDlg == NULL)
    return false;
  if (nPlace < 0) {
    statcmd_hide_tooltip();
    return true;
  }
  ctrl_t *pCtrl = ctrl_find_ndx(sgpStatCmdDlg, static_cast<short>(nPlace + 1));
  if (pCtrl == NULL || (pCtrl->dwFlags & 8) == 0 || ctrl_p26(pCtrl) == NULL)
    return false;
  sgpStatCmdDlg->pFocus = pCtrl;
  statcmd_show_btn_tooltip(sgpStatCmdDlg, pCtrl);
  return true;
}

void DrawTooltipText(char *pszText)
{
  if (StatCmdHudTip(pszText))  // Hyperion: the HUD's tooltip
    return;
  void *pSaved;
  bmp_t *pSurf;
  rect16_t rc;
  ctrl_t *pDlg;
  ctrl_t *pFocus;
  int nW;
  int nH;
  int nOver;

  pSaved = _bmp_current;
  pSurf = ctxhelp_get_surface();
  _bmp_current = pSurf;
  bmp_clear(sgbPens_g[15]);
  font_set(sgpFonts_g[0]);
  font_set_color_tbl(2);
  gbFontJust = 0x11;
  gsFontMargins.left = 2;
  gsFontMargins.top = 0;
  gsFontMargins.right = pSurf->nWidth - 1;
  gsFontMargins.bottom = pSurf->nHeight - 1;
  gbFontLineExtra = 0;
  font_adjust(2, 0, pszText);
  rc = gFontRect;
  sub_4A0DC0(&rc);
  font_set(0);

  pDlg = sgpStatCmdDlg;
  pFocus = pDlg->pFocus;
  nW = rc.right - rc.left + 1;
  nH = rc.bottom - rc.top + 1;
  rc.left = pFocus->rc.left + pDlg->rc.left;
  rc.right = rc.left + nW - 1;
  nOver = g_portView.nWidth - 1 - rc.right;  // Hyperion: the view's edge, 0x27F
  if (nOver < 0) {
    rc.left += nOver;
    rc.right += nOver;
  }
  rc.bottom = pFocus->rc.top + pDlg->rc.top - 1;
  rc.top = rc.bottom - nH + 1;
  // Hyperion: off the HUD's panels
  PortHudHelpPlace(pFocus->rc.left + pDlg->rc.left, pFocus->rc.top + pDlg->rc.top, &rc);

  _bmp_pen = sgbPens_g[16];
  bmp_rect(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1);
  _bmp_current = pSaved;
  img_ctxhelp_invalid(&rc);
}

//
// The command card dialog's event handler. Case order is the oracle's block
// order (14, 3, 15, then 0/1); the jump table at 0x4A0BDC says which tags share
// an arm.
BOOL statcmd_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  switch (pEvent->wTag) {
    case 14:
      // Written with the zero test first so the init arm lands after the
      // dwId == 4 test, where the oracle has it.
      if (pEvent->dwId != 0) {
        if (pEvent->dwId == 4)
          return 1;
      } else {
        ctrl_set_evt_fcns(pDlg, sStatCmdEvtFcnTbl_g, 0x28);
        status_get_console_art(pDlg);
      }
      break;

    case 3:
      statcmd_dlg_mouse_moved(pDlg, pEvent);
      break;

    case 15:
      return statcmd_dlg_char_evn(pEvent);

    case 0:
    case 1:
      return 0;
  }

  return ctrl_dlg(pDlg, pEvent);
}

//
// Draw one command button: its frame (pressed or not), then its icon centred
// in it with the tooltip-icon palette chosen by state, nudged for the replay
// speed buttons.
void statcmd_btn_ctrl_draw_fn(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                              rect16_t *prcClip)
{
  rect32_t rcDraw;
  FRAME frame;
  grhsize_t size;
  int nPressed;
  int nW;
  int nH;
  int nX;
  int nY;
  int i;

  grh_get_header(static_cast<GROUP *>(gpCmdBtns),
                 static_cast<WORD>((pCtrl->dwFlags >> 30) & 1), &frame, &size);
  nW = pCtrl->rc.right - pCtrl->rc.left + 1;
  if (frame.bWidth < nW)
    nW = frame.bWidth;
  nH = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  if (frame.bHeight < nH)
    nH = frame.bHeight;
  SetRect(&rcDraw, 0, 0, nW, nH);
  // Hyperion: the HUD's wells are the frames on its console (port_console.cpp)
  if (!gPlayback && !PortConsoleHudOn())
    rle_draw_normal(pCtrl->rc.left, pCtrl->rc.top, &frame, &rcDraw, 0);

  if (pCtrl->dwFlags & 2)
    i = 1;
  else if (pCtrl->dwFlags & 0x40000000)
    i = 2;
  else
    i = ctrl_w24(pCtrl) == word_6AAB00 ? 2 : 0;
  memcpy(_gbRemapColorTbl, sgBmpTIcon_g + i * 16, 16);

  grh_get_header(static_cast<GROUP *>(gpCmdIcons), ctrl_w24(pCtrl), &frame,
                 &size);
  SetRect(&rcDraw, 0, 0, frame.bWidth, frame.bHeight);
  nPressed = (pCtrl->dwFlags >> 30) & 1;
  nW = pCtrl->rc.right - pCtrl->rc.left + 1;
  nH = pCtrl->rc.bottom - pCtrl->rc.top + 1;
  nX = 0;
  nY = 0;
  if (gPlayback) {
    switch (ctrl_w24(pCtrl)) {
      case 0x182:
      case 0x183:
        nX = -2;
        nY = 3;
        break;
      case 0x185:
        nX = -3;
        nY = 3;
        break;
      case 0x184:
        nX = -4;
        nY = 3;
        break;
    }
  }
  // Hyperion: a mod's own icon the GPU draws at the HD scale is left out
  if (!PortConsoleModUi(MOD_ART_ICON, ctrl_w24(pCtrl)))
    rle_draw_normal((nW - frame.bWidth) / 2 + pCtrl->rc.left + nX + nPressed,
                    (nH - frame.bHeight) / 2 + pCtrl->rc.top + nY + nPressed,
                    &frame, &rcDraw, 0);

  for (i = 0; i < 16; i++)
    _gbRemapColorTbl[i] = static_cast<BYTE>(i);
}

// 0x4A0DC0 sub_4A0DC0 (191 B).
// Draw the tooltip's cost line under the text at prc: an icon and a number for
// each nonzero entry, then widen prc to the line and grow it by one icon row.
// The memset, and the icon call written in both arms of a test on nX, give
// 1.17's zeroing and register order; MSVC merges the two calls.
void sub_4A0DC0(rect16_t *prc)
{
  bmp_t bmp;
  char szNum[0x10];
  int nX = 2;
  int bAny = 0;
  int i;

  memset(&bmp, 0, sizeof(bmp));

  for (i = 0; i < 8; i++) {
    if (sgwStatCmdTooltipData_g[i] != 0) {
      BYTE bFrame = sgbTooltipIcon_g[i];
      if (bFrame == 1)
        bFrame = byte_5264C2 + 1;
      if (nX)
        grp_to_bmp(gpIconsGrp, bFrame, &bmp);
      else
        grp_to_bmp(gpIconsGrp, bFrame, &bmp);
      bmp_mask(nX, prc->bottom, &bmp);
      nX += bmp.nWidth;
      psnprintf(szNum, sizeof(szNum), "%d", sgwStatCmdTooltipData_g[i]);
      font_adjust(nX + 2, prc->bottom, szNum);
      nX = gFontRect.right;
      bAny = 1;
    }
  }

  if (bAny) {
    if (nX > prc->right)
      prc->right = nX;
    prc->bottom += bmp.nHeight + 1;
  }
}

void request_statcmd_update()
{
  sgbForceStatCmdUpdate_g = 1;
}

//
// "statcmd.cpp" and 1107 / 1109 are 1.17's own file and line tags. Each pointer
// is read into a temporary before the store that precedes its call, which keeps
// 1.17's order of the load and the store in the DLL build.
void statcmd_deactivate()
{
  if (sgpStatCmdDlg != NULL)
    dlg_deactivate(sgpStatCmdDlg);

  void *pBtns = gpCmdBtns;
  sgpStatCmdDlg = NULL;
  mem_free(pBtns, "statcmd.cpp", 0x453);

  void *pIcons = gpCmdIcons;
  gpCmdBtns = NULL;
  mem_free(pIcons, "statcmd.cpp", 0x455);

  gpCmdIcons = NULL;
}

//
// Hides the context help and zeroes the tooltip record in one chained
// assignment of nine WORDs: one shared zero gives 1.17's register stores, which
// MSVC merges into its four dword stores, and since a chain stores its rightmost
// target first, writing it from the level down comes out ascending, as 1.17's.
void statcmd_hide_tooltip()
{
  ctxhelp_hide();
  sgwStatCmdTooltipLevel_g = sgwStatCmdTooltipData_g[7] =
      sgwStatCmdTooltipData_g[6] = sgwStatCmdTooltipData_g[5] =
      sgwStatCmdTooltipData_g[4] = sgwStatCmdTooltipData_g[3] =
      sgwStatCmdTooltipData_g[2] = sgwStatCmdTooltipData_g[1] =
      sgwStatCmdTooltipData_g[0] = 0;
}

//
// Reset the card state, load the button and icon groups and the tooltip icon,
// then load and activate the race's command card dialog. A replay always uses
// the 'n' dialog.
void statcmd_init()
{
  char szPath[0x104];

  word_6AAAF4 = sgwSpecialCard = sgCard = 0xE4;
  word_6AAB00 = 0xFFFF;

  // Hyperion: the console's race, Console skin's (PortConsoleRace), here and below
  psnprintf(szPath, sizeof(szPath), "%s%ccmdbtns.grp", "unit\\cmdbtns\\",
            gszRaceChars_g[PortConsoleRace()]);
  gpCmdBtns = ReadGroupFile(szPath, 0, "statcmd.cpp", 0x43E);
  gpCmdIcons = ReadGroupFile("unit\\cmdbtns\\cmdicons.grp", 0, "statcmd.cpp",
                             0x441);
  gpCmdIcons = ModConsoleGroup(MOD_ART_ICON, static_cast<GROUP *>(gpCmdIcons));  // Hyperion
  GameLoadImage(reinterpret_cast<intptr_t>("unit\\cmdbtns\\ticon.pcx"), 0,
                reinterpret_cast<intptr_t>(sgBmpTIcon_g), 0x60, 0, 0, 0);

  psnprintf(szPath, sizeof(szPath), "rez\\statbtn%c.bin",
            gPlayback ? 'n' : gszRaceChars_g[PortConsoleRace()]);
  sgpStatCmdDlg = dlg_load(szPath,
                           reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                           reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                           "statcmd.cpp", 0x448);
  dlg_activate(sgpStatCmdDlg, 0, statcmd_dlg_fcn);
}

//
// The command card's own mouse move: finds the control under the cursor
// (nothing while the game is paused) and, when it changed, records it and
// tail-calls statcmd_show_btn_tooltip.
void statcmd_dlg_mouse_moved(ctrl_t *pDlg, dlgevt_t *pEvent)
{
  ctrl_t *pHit = IsGamePaused() ? NULL : ctrl_find(pDlg, pEvent);
  if (pHit != sgpStatCmdHoverCtrl_g) {
    sgpStatCmdHoverCtrl_g = pHit;
    statcmd_show_btn_tooltip(pDlg, pHit);
  }
}

//
// Deal the current card's buttons onto the command card's controls: each
// control from index 1 takes the next button whose availability test passes,
// and a control nothing lands on is hidden. The walk stops at the first control
// with an index below 1. The card is a pointer local, as 1.17 addresses it.
void statcmd_set_action_btns()
{
  statcmd_card_t *pCard = StatcmdCard(sgCard);  // Hyperion: a mod's unit's own
  int nAvail = 0;
  WORD i = 0;
  statcmd_btn_t *pBtn = pCard->pBtns;
  ctrl_t *pCtrl;

  for (pCtrl = ctrl_find_ndx(sgpStatCmdDlg, 1); pCtrl != NULL;
       pCtrl = pCtrl->pNext) {
    if (pCtrl->nIndex < 1)
      return;
    if (pCtrl->nIndex > 9)
      continue;

    for (; i < pCard->wBtnCount; i++, pBtn++) {
      nAvail = pBtn->pfnCondition(pBtn->bConditionArg,
                                  static_cast<BYTE>(g_LocalNationID),
                                  gpStatUnit);
      if (nAvail != 0)
        break;
    }

    if ((gPlayback || gpStatUnit == NULL ||
         gpStatUnit->bPlayer == g_LocalNationID) &&
        i < pCard->wBtnCount && pCtrl->nIndex >= pBtn->wMinIndex) {
      ctrl_p26(pCtrl) = pBtn;
      if (ctrl_w24(pCtrl) != pBtn->wIcon) {
        ctrl_w24(pCtrl) = pBtn->wIcon;
        ctrl_set_update(pCtrl);
        ctrl_send_msg(pCtrl, 6, 0);
      }
      ctrl_show(pCtrl);
      if (nAvail < 0) {
        ctrl_gray(pCtrl);
        if (get_allowed_err() == 0x15)
          pBtn->wDisabledStrId = 0x2FA;
      } else {
        ctrl_ungray(pCtrl);
      }
      pBtn++;
      i++;
    } else {
      ctrl_hide(pCtrl);
      ctrl_p26(pCtrl) = NULL;
      ctrl_w24(pCtrl) = 0xFFFF;
    }
  }
}

void statcmd_set_ctrl(int nCard)
{
  sgwSpecialCard = static_cast<WORD>(nCard);
  statcmd_update();
}

//
// Eight WORD stores into the tooltip record: the two register arguments at
// +0x00 and +0x02, the six stack arguments at +0x04 to +0x0E.
void statcmd_set_tooltip_data(WORD wArg0, WORD wArg1, WORD wArg2, WORD wArg3,
                              WORD wArg4, WORD wArg5, WORD wArg6, WORD wArg7)
{
  sgwStatCmdTooltipData_g[0] = wArg0;
  sgwStatCmdTooltipData_g[1] = wArg1;
  sgwStatCmdTooltipData_g[2] = wArg2;
  sgwStatCmdTooltipData_g[3] = wArg3;
  sgwStatCmdTooltipData_g[4] = wArg4;
  sgwStatCmdTooltipData_g[5] = wArg5;
  sgwStatCmdTooltipData_g[6] = wArg6;
  sgwStatCmdTooltipData_g[7] = wArg7;
}

static void __fastcall order_mod_cast(WORD wOrder, BYTE bQueued);  // Hyperion
static void __fastcall order_mod_target(WORD wOrder, BYTE);

// 0x4A1240 statcmd_show_btn_tooltip (837 B).
// Show the tooltip for the button under the cursor. A grayed button shows its
// disabled-reason string; otherwise the second character of its status string
// picks the cost line (unit, upgrade, tech energy, tech cost, unit cost alone).
void statcmd_show_btn_tooltip(ctrl_t *pDlg, ctrl_t *pCtrl)
{
  char *psz;
  BYTE bKind;
  statcmd_btn_t *pBtn;

  if (pDlg != pCtrl) {
    if (pCtrl != NULL) {
      if (pCtrl->dwFlags & 8) {
        pBtn = ctrl_p26(pCtrl);
        psz = status_get_str(pBtn->wStatusStrId);
        bKind = psz[1];
        if (pCtrl->dwFlags & 2) {
          WORD wStr;

          pDlg->pFocus = pCtrl;
          sgwStatCmdTooltipLevel_g = sgwStatCmdTooltipData_g[7] =
              sgwStatCmdTooltipData_g[6] = sgwStatCmdTooltipData_g[5] =
              sgwStatCmdTooltipData_g[4] = sgwStatCmdTooltipData_g[3] =
              sgwStatCmdTooltipData_g[2] = sgwStatCmdTooltipData_g[1] =
              sgwStatCmdTooltipData_g[0] = 0;
          // Testing the field rather than wStr keeps wStr out of a register.
          if (pBtn->wDisabledStrId == 0) {
            statcmd_hide_tooltip();
            return;
          }
          wStr = pBtn->wDisabledStrId;
          if (bKind == 2) {
            BYTE bPlayer = gpStatUnit->bPlayer;
            wStr += get_upgrade_level(bPlayer, pBtn->w0E);
          }
          statcmd_show_tooltip(status_get_str(wStr));
          return;
        }

        ctrl_set_tabstop(pCtrl);
        // The level is a one-field struct, 1.17's join of the cases; a plain
        // WORD local adds a widening after the increment.
        struct { WORD w; } level;
        level.w = 0;
        switch (bKind) {
          case 1: {
            WORD wType = pBtn->w0E;
            BYTE bCount = (gulUnitTypeFlags[wType] & 0x400) != 0 ? 2 : 1;
            BYTE bInfo = gubUnitCountInfo[wType];
            WORD w4 = static_cast<WORD>(bCount * ((bInfo & 4) ? gubUnitFoodUsed[wType] : 0));
            WORD w2 = static_cast<WORD>(bCount * ((bInfo & 2) ? gubUnitFoodUsed[wType] : 0));
            WORD w1 = static_cast<WORD>(bCount * ((bInfo & 1) ? gubUnitFoodUsed[wType] : 0));
            statcmd_set_tooltip_data(guwUnitMineralsCost[wType],
                                     guwUnitGasesCost[wType], 0, 0, 0, w4 >> 1,
                                     w2 >> 1, w1 >> 1);
            break;
          }
          case 5:
            statcmd_set_tooltip_data(guwUnitMineralsCost[pBtn->w0E],
                                     guwUnitGasesCost[pBtn->w0E], 0, 0, 0, 0,
                                     0, 0);
            break;
          case 2: {
            BYTE bPlayer = gpStatUnit->bPlayer;
            statcmd_set_tooltip_data(
                get_upgrade_ore_cost(bPlayer, pBtn->w0E),  // Hyperion: the whole WORD
                get_upgrade_gas_cost(bPlayer, pBtn->w0E), 0,
                0, 0, 0, 0, 0);
            if (upg_repeats[pBtn->w0E] > 1) {
              BYTE bOwner = gpStatUnit->bPlayer;
              level.w = static_cast<WORD>(get_upgrade_level(bOwner, pBtn->w0E) + 1);
            }
            break;
          }
          case 3: {
            WORD wTech = pBtn->w0E;
            // Hyperion: a mod's cast keeps its order in w0E and its tech in the
            // condition's argument
            if (pBtn->pfnAction == order_mod_cast || pBtn->pfnAction == order_mod_target)
              wTech = pBtn->bConditionArg;
            BYTE bRace = gpStatUnit->GetRace();
            statcmd_set_tooltip_data(0, 0,
                                     bRace == 2 ? techdata_energy_g[wTech] : 0,
                                     bRace == 1 ? techdata_energy_g[wTech] : 0,
                                     bRace == 0 ? techdata_energy_g[wTech] : 0,
                                     0, 0, 0);
            break;
          }
          case 4:
            statcmd_set_tooltip_data(techdata_oreCost_g[pBtn->w0E],
                                     techdata_gasCost_g[pBtn->w0E], 0, 0, 0, 0,
                                     0, 0);
            break;
          default:
            sgwStatCmdTooltipData_g[7] = sgwStatCmdTooltipData_g[6] =
                sgwStatCmdTooltipData_g[5] = sgwStatCmdTooltipData_g[4] =
                sgwStatCmdTooltipData_g[3] = sgwStatCmdTooltipData_g[2] =
                sgwStatCmdTooltipData_g[1] = sgwStatCmdTooltipData_g[0] = 0;
            break;
        }
        sgwStatCmdTooltipLevel_g = level.w;
        // Hyperion: the key the button has now, as the profile sets it (K1)
        statcmd_show_tooltip(PortCardTooltip(pBtn->wStatusStrId, pCtrl->nIndex, psz));
        return;
      }
    } else {
      if (pDlg->pFocus == NULL)
        return;
      ctrl_set_update(pDlg->pFocus);
      pDlg->pFocus = NULL;
    }
  }
  statcmd_hide_tooltip();
}

// Walk the selected units and pick the command card's buttonset, plus three
// population counts that choose a fallback set. The unit-flags row is read
// again after the GetRace call, and the loop index is an int.
void sub_4A15A0()
{
  BYTE bCount8;
  BYTE bCount200;
  BYTE bCountRace0;
  BYTE bAny100000;
  int i;
  // These five statements are in 1.17's order of its zero registers and its
  // prologue stores.
  bCount8 = 0;
  bCount200 = 0;
  bAny100000 = 0;
  bCountRace0 = 0;
  WORD wCur = gpStatUnit->wButtonset;

  // Hyperion: in a game of Hyperion's selection the card is the console's subgroup's,
  // the units of gpStatUnit's type, and the three counts are against theirs
  const bool bSubgroup = SelectionWide();
  BYTE bScanned = 0;
  for (i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = StatUnits()[i];
    WORD wBtnset;

    if (pUnit == NULL)
      continue;
    if (bSubgroup && pUnit->wType != gpStatUnit->wType)
      continue;
    bScanned++;

    wBtnset = pUnit->wButtonset;

    if ((gulUnitTypeFlags[pUnit->wType] & 8) != 0)
      bCount8++;
    if ((gulUnitTypeFlags[pUnit->wType] & 0x200) != 0)
      bCount200++;
    if ((pUnit->dwFlags & 4) == 0 && pUnit->GetRace() == 0)
      bCountRace0++;
    if ((gulUnitTypeFlags[pUnit->wType] & 0x100000) != 0)
      bAny100000 = 1;

    if (wBtnset != wCur) {
      // Hyperion: StatcmdCard, a mod's unit's own card too
      if (StatcmdCard(wBtnset)->pBtns != StatcmdCard(wCur)->pBtns &&
          StatcmdCard(wBtnset)->wLink != wCur) {
        if (StatcmdCard(wCur)->wLink == wBtnset) {
          word_6AAAF4 = wBtnset;
          wCur = wBtnset;
        } else {
          word_6AAAF4 = 0xF4;
        }
      }
    }
  }

  if (word_6AAAF4 == 0xE4)
    return;

  const BYTE bCount = bSubgroup ? bScanned : byte_6AB4C0;
  if (bCount == bCount8) {
    word_6AAAF4 = 0xF5;
    return;
  }
  if (bCount == bCount200) {
    word_6AAAF4 = 0xF6;
    return;
  }
  if (bAny100000 != 0 && bCount == bCountRace0)
    word_6AAAF4 = 0xF7;
}

//
// Draws the string with the upgrade level's line under it when there is a
// level, the string alone when there is none, and hides the tooltip when there
// is no string.
void statcmd_show_tooltip(const char *psz)
{
  char szBuf[0x80];

  if (sgwStatCmdTooltipLevel_g != 0 && psz != NULL) {
    psnprintf(szBuf, sizeof(szBuf), "%s\n%s %d", psz,
              status_get_str(0x31B), sgwStatCmdTooltipLevel_g);
    DrawTooltipText(szBuf);
  } else if (psz != NULL) {
    DrawTooltipText(const_cast<char *>(psz));
  } else {
    ctxhelp_hide();
  }
}

// 0x4A1760 statcmd_update (353 B).
//
// Rebuild the command card when its card changed or a rebuild was requested;
// otherwise only re-highlight when the unit's orders changed. Each card source
// keeps its own compare and store; the compiler merges them after layout.
void statcmd_update()
{
  dlgevt_t evt;
  WORD wCard;
  CUnit *pUnit;
  DWORD bForce = sgbForceStatCmdUpdate_g;

  wCard = sgwSpecialCard;
  if (bForce && (wCard == 0xE4 || gPlayback)) {
    sub_4A18D0();
    bForce = sgbForceStatCmdUpdate_g;
    wCard = sgwSpecialCard;
  }

  pUnit = gpStatUnit;
  if (wCard != 0xE4) {
    if (sgCard == wCard)
      goto same;
    sgCard = wCard;
    sgbForceStatCmdUpdate_g = 1;
    goto rebuild;
  } else {
    wCard = word_6AAAF4;
    if (wCard != 0xE4) {
      if (sgCard == wCard)
        goto same;
      sgCard = wCard;
      // Two identical arms, for 1.17's layout; they compile to one store.
      if (bForce && (wCard == 0xE4 || gPlayback))
        sgbForceStatCmdUpdate_g = 1;
      else
        sgbForceStatCmdUpdate_g = 1;
      goto rebuild;
    } else {
      if (pUnit == NULL)
        goto same;
      // The buttonset is read as an int, so this path is not joined before layout.
      int nSet = pUnit->wButtonset;
      if (sgCard == (WORD)nSet)
        goto same;
      sgCard = (WORD)nSet;
      sgbForceStatCmdUpdate_g = 1;
      goto rebuild;
    }
  }
same:
  if (!bForce) {
    if (pUnit != NULL) {
      order_id_t bOrder = pUnit->bOrderId;
      order_id_t bSubOrder = pUnit->subunit ? pUnit->subunit->bOrderId : kNoSubOrder;
      if (byte_6AAAF2 != bOrder || byte_6AAAF3 != bSubOrder) {
        sub_4A1950();
        byte_6AAAF2 = bOrder;
        byte_6AAAF3 = bSubOrder;
        sub_4A1950();
      }
    }
    return;
  }
rebuild:
  statcmd_set_action_btns();
  {
    LONG anCursor[2];
    PortHudConsoleCursor(anCursor);  // Hyperion: where the HUD shows the card
    evt.nX = static_cast<short>(anCursor[0]);
    evt.nY = static_cast<short>(anCursor[1]);
  }
  ctrl_t *pHit = ctrl_find(sgpStatCmdDlg, &evt);
  statcmd_show_btn_tooltip(sgpStatCmdDlg, pHit);
  sgpStatCmdHoverCtrl_g = pHit;
  CUnit *pSel = gpStatUnit;
  if (pSel != NULL) {
    CUnit *pSub = pSel->subunit;
    order_id_t bOrder = pSel->bOrderId;
    order_id_t bSubOrder = pSub ? pSub->bOrderId : kNoSubOrder;
    sub_4A1950();
    byte_6AAAF2 = bOrder;
    byte_6AAAF3 = bSubOrder;
    sub_4A1950();
  }
  if (gPlayback)
    statdraw_replay_progress(sgpStatCmdDlg);
  sgbForceStatCmdUpdate_g = 0;
}

//
// Pick the special card: replay play/pause, targeting, placement, or none, then
// let sub_4A15A0 choose a shared buttonset for a multi-unit selection.
void sub_4A18D0()
{
  sgwSpecialCard = 0xE4;
  sgCard = 0xE4;
  if (gPlayback) {
    sgwSpecialCard = sgbReplayPaused_g ? 0xF8 : 0xF9;
  } else if (byte_6AB4C0) {
    if (target_is_on())
      sgwSpecialCard = 0xE5;
    else if (placebox_is_on())
      sgwSpecialCard = 0xE6;
  }
  word_6AAAF4 = 0xE4;
  if (byte_6AB4C0 > 1 && !gPlayback)
    sub_4A15A0();
}

//
// Pick which of the two pending order ids the command card should highlight,
// then find the button carrying it and mark that button for redraw. The
// subunit's is taken when the unit's has no button, or is 0xE5 and the
// subunit's has one.
void sub_4A1950()
{
  WORD w1 = order_highlight[byte_6AAAF2];
  ctrl_t *pCtrl = sgpStatCmdDlg->pChildren;
  WORD w2 = (byte_6AAAF3 == kNoSubOrder) ? 0xFFFF
                                  : static_cast<WORD>(order_highlight[byte_6AAAF3]);

  WORD wFind = w1;
  if (w1 == 0xFFFF || (w2 != 0xFFFF && w1 == 0xE5))
    wFind = w2;

  while (pCtrl != NULL) {
    if ((pCtrl->dwFlags & 8) && ctrl_w24(pCtrl) == wFind) {
      ctrl_set_update(pCtrl);
      word_6AAB00 = wFind;
      return;
    }
    pCtrl = pCtrl->pNext;
  }
  word_6AAB00 = 0xFFFF;
}

// Hyperion: the buttons a mod adds to command cards
// (port_moddata.h's ModCardButtons). A game with the mod has each 1.17 card it adds to
// copied with its buttons put in among 1.17's by position, and a card of each mod's unit
// it names, its looks' card with the buttons; any other game has 1.17's cards. The
// condition's argument, 16 bits on x64, is a train button's unit, a cast's,
// research's or upgrade's tech or upgrade.
#include <vector>
#include "../h/typelimits.h"
#include "../port/data/port_moddata.h"
#include "../h/target.h"  // target_on

static int __fastcall bf_mod_unit_ok(int nArg, int nPlayer, CUnit *pUnit)
{
  return bf_building_ok(static_cast<WORD>(nArg), nPlayer, pUnit);
}

static int __fastcall bf_mod_spell_ok(int nTech, int nPlayer, CUnit *pUnit)
{
  return bf_has_spell(static_cast<tech_id_t>(nTech), nPlayer, pUnit);
}

static int __fastcall bf_mod_research_ok(int nTech, int nPlayer, CUnit *pUnit)
{
  return bf_research(static_cast<tech_id_t>(nTech), nPlayer, pUnit);
}

static int __fastcall bf_mod_upgrade_ok(int nUpgrade, int nPlayer, CUnit *pUnit)
{
  return bf_upgrade(static_cast<upgrade_id_t>(nUpgrade), nPlayer, pUnit);
}

// A cast with no target: the order at the place of the unit the panel shows, which is in
// range of every caster (the weapon's range is the map's), so each casts where it stands.
static void __fastcall order_mod_cast(WORD wOrder, BYTE bQueued)
{
  if (gpStatUnit != NULL)
    send_net_cmd_Action(static_cast<WORD>(PosS(gpStatUnit->wPositionX)),
                        static_cast<WORD>(PosS(gpStatUnit->wPositionY)), NULL, 0xE4,
                        wOrder, bQueued);
}

// An aimed cast: targeting, as order_tech enters it for 1.17's spells once the selection
// has the energy, with the mod's order for a unit, the ground and the fog alike. The
// button's argument is the order, and the tech is the order's, orders.dat's tech column
// (an order past 1.17's byte no longer shares the argument with it).
static void __fastcall order_mod_target(WORD wOrder, BYTE)
{
  if (!order_can_cast_spell(order_techType[wOrder]))
    return;
  target_on(wOrder, wOrder, wOrder);
}

struct StatcmdModCard {
  WORD wCard;
  statcmd_card_t saved;  // a 1.17 card's own, put back; the looks' card of a mod's unit
  statcmd_card_t own;    // a mod's unit's card
  std::vector<statcmd_btn_t> btns;
};
static std::vector<StatcmdModCard> s_modCards;

statcmd_card_t *StatcmdCard(WORD wCard)
{
  if (UnitTypeMod(wCard))
    for (StatcmdModCard &c : s_modCards)
      if (c.wCard == wCard)
        return &c.own;
  return &gCards[wCard];
}

// Hyperion: the engine's copies of the buttons EUD maps point cards at.
static std::vector<statcmd_btn_t> s_eudBtns[250];

void StatcmdEudButtons(WORD wCard, const BYTE *pButtons, int nButtons, void *const *ppfn)
{
  if (wCard >= 250)
    return;
  std::vector<statcmd_btn_t> &v = s_eudBtns[wCard];
  v.assign(nButtons > 0 ? nButtons : 1, statcmd_btn_t());
  for (int i = 0; i < nButtons; i++) {
    const BYTE *pb = pButtons + 20 * i;
    statcmd_btn_t &b = v[i];
    memcpy(&b.wMinIndex, pb, 2);
    memcpy(&b.wIcon, pb + 2, 2);
    b.pfnCondition = reinterpret_cast<int(__fastcall *)(int, int, CUnit *)>(ppfn[2 * i]);
    b.pfnAction = reinterpret_cast<void(__fastcall *)(WORD, BYTE)>(ppfn[2 * i + 1]);
    b.bConditionArg = pb[0xC];
    b._unmapped0D = pb[0xD];
    memcpy(&b.w0E, pb + 0xE, 2);
    memcpy(&b.wStatusStrId, pb + 0x10, 2);
    memcpy(&b.wDisabledStrId, pb + 0x12, 2);
  }
  gCards[wCard].pBtns = v.data();
}

void StatcmdEudButtonsClear()
{
  for (std::vector<statcmd_btn_t> &v : s_eudBtns)
    std::vector<statcmd_btn_t>().swap(v);
}

void StatcmdModCards(bool bApply)
{
  // A card's id past 1.17's units is a mod's unit's own, whatever game is set up.
  for (const StatcmdModCard &c : s_modCards)
    if (c.wCard < UNIT_TYPE_MOD_FIRST)
      gCards[c.wCard] = c.saved;
  s_modCards.clear();
  int n = 0;
  const ModCardButton *pAdd = ModCardButtons(&n);
  if (!bApply)
    return;
  for (int k = 0; k < n; ++k) {
    StatcmdModCard *pCard = nullptr;
    for (StatcmdModCard &c : s_modCards)
      if (c.wCard == pAdd[k].wCard)
        pCard = &c;
    if (!pCard) {
      s_modCards.push_back(StatcmdModCard());
      pCard = &s_modCards.back();
      pCard->wCard = pAdd[k].wCard;
      const WORD wFrom = pAdd[k].wCard >= UNIT_TYPE_MOD_FIRST ? static_cast<WORD>(UnitTypeLooks(pAdd[k].wCard))
                                                              : pAdd[k].wCard;
      pCard->saved = gCards[wFrom];
      pCard->btns.assign(pCard->saved.pBtns, pCard->saved.pBtns + pCard->saved.wBtnCount);
    }
    statcmd_btn_t b = {};
    b.wMinIndex = pAdd[k].wPos;
    b.wIcon = pAdd[k].wIcon;
    if (pAdd[k].wUnit != 0xFFFF) {
      b.pfnCondition = bf_mod_unit_ok;
      b.pfnAction = order_build;
      b.bConditionArg = pAdd[k].wUnit;
      b.w0E = pAdd[k].wUnit;
    } else if (pAdd[k].bUpgrade) {
      b.pfnCondition = bf_mod_upgrade_ok;
      b.pfnAction = order_upgrade;
      b.bConditionArg = pAdd[k].wTech;
      b.w0E = pAdd[k].wTech;
    } else if (pAdd[k].bResearch) {
      b.pfnCondition = bf_mod_research_ok;
      b.pfnAction = order_research;
      b.bConditionArg = pAdd[k].wTech;
      b.w0E = pAdd[k].wTech;
    } else {
      b.pfnCondition = bf_mod_spell_ok;
      b.pfnAction = pAdd[k].bTarget ? order_mod_target : order_mod_cast;
      b.bConditionArg = pAdd[k].wTech;
      b.w0E = pAdd[k].wOrder;
    }
    b.wStatusStrId = pAdd[k].wStr;
    b.wDisabledStrId = pAdd[k].wDisabledStr;
    size_t at = 0;
    while (at < pCard->btns.size() && pCard->btns[at].wMinIndex <= b.wMinIndex)
      ++at;
    pCard->btns.insert(pCard->btns.begin() + at, b);
  }
  for (StatcmdModCard &c : s_modCards) {
    statcmd_card_t &card = c.wCard >= UNIT_TYPE_MOD_FIRST ? c.own : gCards[c.wCard];
    card = c.saved;
    card.pBtns = c.btns.data();
    card.wBtnCount = static_cast<WORD>(c.btns.size());
  }
}
