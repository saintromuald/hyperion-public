// opt_dlgs.cpp -- the in-game menu stack: the F10 game menu and its abort,
// options, help, quit and quit-to-menu children, the mission-objectives dialog,
// the dialog-frame tile GRP loader, and the option-dialog alloc, free and close
// plumbing they all share. Bodies are in oracle VA order.
//
// Every `*_respond` takes the dialog, though three never read it, because all
// of them go into the one sgpRespondFcn slot (opt_dlgs.h).

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>   // memset, strcpy, _stricmp

#include "../h/opt_dlgs.h"
#include "../h/dlg_effects.h"
#include "../h/gluRdy.h"        // bmp_create
#include "../h/net_misc.h"    // IsBNetGame
#include "../h/snd.h"           // snd_set_options, is_sfx_enabled
#include "../h/save_dlg.h"    // modeless_dlg_startup, modeless_opt_dlg, CanSaveGame
#include "../gen/globals117.h"
#include "../h/dlg_dlg.h"     // dlg_deactivate, app_add_exit, set_digital_volume
#include "../h/dlg_ctrl.h"    // ctrl_find_ndx, ctrl_hide, ctrl_set_gray, ...
#include "../h/dlg_dlgdraw.h" // rendertype_t, sub_4C4D50
#include "../h/minimap.h"     // timer_create, timer_remove_node, PFN_TIMER_FN
#include "../h/net_cmds.h"    // gameloop_init, IsGameHost, IsGamePaused, ...
#include "../h/gametype.h"    // IsLadderGame, Is*Game, gametype_get_subtype_value
#include "../h/pause.h"       // player_can_pause
#include "../h/maphdr.h"      // is_color_cycled
#include "../h/TrigAct.h"     // get_map_string
#include "../h/Storm.h"       // SMemAlloc, SMemFree
#include "../h/bmp.h"         // bmp_t, bmp_clear
#include "../h/grp.h"         // GROUP / FRAME
#include "../h/rect32.h"      // rect32_t
#include "../h/statdata.h"    // grhsize_t (grh_get_header's out-parameter)
#include "../port/gameplay/port_score.h"  // Hyperion: PortScoreLeft
#include "../port/front/port_front.h"     // Hyperion: PortFrontScoreAfterLeave
#include "../port/front/port_gamemenu.h"  // Hyperion: Hyperion's game menu
#include "../port/tools/port_bench.h"      // Hyperion: PortBenchPlaying
#include "../port/tools/port_benchview.h"  // Hyperion: PortBenchViewMenu

// The strcpy in sub_47B760 is the /Oi intrinsic, as in 1.17.
#pragma intrinsic(strcpy)

// Callees in other TUs, declared here rather than in a header; where another TU
// already spells one, the spelling is copied.

// Storm's ordinal 145, linked by its undecorated name through
// storm_imports.def, hence the extern "C"; __stdcall with one stack word.
extern "C" BOOL __stdcall SNetLeagueLogout(char *pszName);

// SetRect is <windows.h>'s own declaration, which Storm.h and maphdr.h bring in,
// so this TU cannot declare it again; sub_47B3C0 casts its rect32_t, the same
// four longs, to LPRECT.

// mem_free: ECX the block, EDX the __FILE__ string, one stack word the line,
// spelled as gametype.cpp and maphdr.cpp spell it.
void mem_free(void *pv, const char *pszTag, int nLine);

// game_reset_handlers (0x448DD0), nullary, spelled as event_Win.h spells it.
void game_reset_handlers();

// effects_init (0x4CA420) is declared in dlg_effects.h: ECX is the game palette,
// an object, and EDX the descriptor built on this frame.

// snd_set_options (0x49EA60) and is_sfx_enabled (0x49D810) are declared in
// snd.h.

// grh_get_header (0x463BD0), `ret 8h`, spelled as statdata.cpp spells it. The
// frame is a WORD.
void __fastcall grh_get_header(GROUP *pGroup, WORD wFrame, FRAME *pFrame,
                               grhsize_t *pSize);

// rle_draw_normal (0x41D630), spelled as CImage.h declares it.
void __fastcall rle_draw_normal(int nX, int nY, void *pGraphic, void *pDraw,
                                BYTE *pRemap);
// bmp_create (0x4CB200) is declared in gluRdy.h and gds_metabmp.h.

// ReadGroupFile (0x444A60): ECX the archive path, EDX a flags dword, then the
// __FILE__ and line pair; it returns the GROUP the tile cache keeps.
GROUP *__fastcall ReadGroupFile(const char *pszFile, intptr_t nFlags,
                                const char *pszTag, int nLine);

// 0x46C870 and 0x46C840, spelled as replay.cpp and save_dlg.cpp spell them.
void __fastcall set_size_escape(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
void __fastcall set_size_enter(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
// sub_4CA820 is declared in dlg_effects.h.

// 0x4B8E80 (3 B), `xor eax, eax / ret`, a nullary stub several empty functions
// share; menu_activate tests its result.
int __fastcall nullsub___shared_();

// sub_476F60 is declared in opt_dlgs.h.

// 0x4BE950 is a one-byte `ret` several empty functions share, and
// quit_to_menu_respond calls the nullary one; net_cmds.h declares the
// two-argument spelling of the same address.
void voice_squelch();

// The bmp_t a ctrl_t carries at +0x0C, which dialog.h models as
// `_unmapped0C[4]`. A macro, as in dlg_draw.cpp.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// opt_dlg_generic_close's +0x26 slot, a pointer inside dialog.h's
// `/*0x24*/ dlg_byte_t _unmapped24[6]`, reached through that array. A macro,
// like ctrl_bmp.
#define opt_dlg_data26(p) (reinterpret_cast<void **>(&(p)->_unmapped24[2]))

// The globals this TU reaches, dual-spelled, one #ifndef per name.

// sgpRespondFcn at 0x675068 -- the deferred menu action; menu_dlg_fcn fires it
// on event id 2.
#ifndef OC117_W80_sgpRespondFcn
#define OC117_W80_sgpRespondFcn
#ifdef OC_DLL_MODE
#define sgpRespondFcn (*reinterpret_cast<PFN_RESPOND **>(OC_VA(0x675068u)))
#else
extern PFN_RESPOND *sgpRespondFcn;
#endif
#endif

// gnDlgReturnVal at 0x6CE6DC, a long.
#ifndef OC117_W80_gnDlgReturnVal
#define OC117_W80_gnDlgReturnVal
#ifdef OC_DLL_MODE
#define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
#else
extern LONG gnDlgReturnVal;
#endif
#endif

// gbDlgLevel at 0x671A81, a BYTE.
#ifndef OC117_W80_gbDlgLevel
#define OC117_W80_gbDlgLevel
#ifdef OC_DLL_MODE
#define gbDlgLevel (*reinterpret_cast<BYTE *>(OC_VA(0x671A81u)))
#else
extern BYTE gbDlgLevel;
#endif
#endif

// sgpTileGrp at 0x67506C -- the cached tile GROUP. Every reader and writer is in
// this TU.
#ifndef OC117_W80_sgpTileGrp
#define OC117_W80_sgpTileGrp
#ifdef OC_DLL_MODE
#define sgpTileGrp (*reinterpret_cast<GROUP **>(OC_VA(0x67506Cu)))
#else
extern GROUP *sgpTileGrp;
#endif
#endif

// sgszTileGrpName at 0x675070 -- the file name the cached GRP was loaded under.
// Its extent is not known, so [1] stands in.
#ifndef OC117_W80_sgszTileGrpName
#define OC117_W80_sgszTileGrpName
#ifdef OC_DLL_MODE
#define sgszTileGrpName (*reinterpret_cast<char (*)[1]>(OC_VA(0x675070u)))
#else
extern char sgszTileGrpName[1];
#endif
#endif

// sgbOptsInited at 0x675174 -- set by opts_init and cleared by opts_free, it
// gates the effects_free teardown. The name is ours.
#ifndef OC117_W80_sgbOptsInited
#define OC117_W80_sgbOptsInited
#ifdef OC_DLL_MODE
#define sgbOptsInited (*reinterpret_cast<LONG *>(OC_VA(0x675174u)))
#else
extern LONG sgbOptsInited;
#endif
#endif

// sgMenuEvtFcnTbl at 0x508D70 -- the F10 menu's per-control event functions,
// eight of them, as menu_activate's ctrl_set_evt_fcns call counts them.
#ifndef OC117_W80_sgMenuEvtFcnTbl
#define OC117_W80_sgMenuEvtFcnTbl
#ifdef OC_DLL_MODE
#define sgMenuEvtFcnTbl (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x508D70u)))
#else
extern PFN_CTRL_EVT sgMenuEvtFcnTbl[8];
#endif
#endif

// gbNationTypes at 0x5349E0 -- a BYTE per nation, which victory.cpp sets; its
// extent is not known.
#ifndef OC117_W80_gbNationTypes
#define OC117_W80_gbNationTypes
#ifdef OC_DLL_MODE
#define gbNationTypes (reinterpret_cast<BYTE *>(OC_VA(0x5349E0u)))
#else
extern BYTE gbNationTypes[1];
#endif
#endif

// 0x536720 -- the expansion gate, a BYTE, under a placeholder name.
#ifndef OC117_W80_b536720
#define OC117_W80_b536720
#ifdef OC_DLL_MODE
#define oc_b536720 (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE oc_b536720;
#endif
#endif

// 0x66A274 -- the pending glue-screen id, a DWORD: here 6 (StarCraft's main
// menu) or 0x16 (Brood War's).
#ifndef OC117_W80_d66A274
#define OC117_W80_d66A274
#ifdef OC_DLL_MODE
#define oc_d66A274 (*reinterpret_cast<DWORD *>(OC_VA(0x66A274u)))
#else
extern DWORD oc_d66A274;
#endif
#endif

// sgSndVolTbl_g @0x4EFC00 .rdata, 100 longs, spelled as snd.cpp spells it. The
// registry names it without a type, so gen emits nothing and this TU declares it.
#ifdef OC_DLL_MODE
#define sgSndVolTbl_g (reinterpret_cast<const LONG *>(OC_VA(0x4EFC00u)))
#else
extern const LONG sgSndVolTbl_g[100];
#endif

// The .rdata dialog templates, objects rather than pointers, named after the
// paths at their addresses: 4EF380 "rez\objctdlg.bin", 4EF394
// "rez\gamemenu.bin", 4EF3A8 "rez\helpmenu.bin", 4EF3BC "rez\options.bin",
// 4EF3CC "rez\abrtmenu.bin", 4EF3E0 "rez\quit.bin", 4EF3F0 "rez\quitrepl.bin",
// 4EF404 "rez\quit2mnu.bin" and 4EF418 "rez\restart.bin".
#ifdef OC_DLL_MODE
#define sgObjectiveDlgTemplate  (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF380u)))
#define sgGameMenuDlgTemplate   (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF394u)))
#define sgHelpMenuDlgTemplate   (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF3A8u)))
#define sgOptsMenuDlgTemplate   (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF3BCu)))
#define sgAbortMenuDlgTemplate  (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF3CCu)))
#define sgQuitDlgTemplate       (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF3E0u)))
#define sgQuitReplayDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF3F0u)))
#define sgQuitToMenuDlgTemplate (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF404u)))
#define sgRestartDlgTemplate    (*reinterpret_cast<BYTE (*)[1]>(OC_VA(0x4EF418u)))
#else
extern BYTE sgObjectiveDlgTemplate[1];
extern BYTE sgGameMenuDlgTemplate[1];
extern BYTE sgHelpMenuDlgTemplate[1];
extern BYTE sgOptsMenuDlgTemplate[1];
extern BYTE sgAbortMenuDlgTemplate[1];
extern BYTE sgQuitDlgTemplate[1];
extern BYTE sgQuitReplayDlgTemplate[1];
extern BYTE sgQuitToMenuDlgTemplate[1];
extern BYTE sgRestartDlgTemplate[1];
#endif

// TU-local types.

// The 0x24-byte options block at 0x6D3130, one object: option_dlg_alloc and
// option_dlg_cmd copy it whole. gen/globals117.h has its leading DWORD, the
// game speed, as gOptions; a body uses one spelling or the other, never both,
// or MSVC takes the round trip for a dead store. +0x10 indexes the volume table.
struct optionsblk_t {
  /*0x00*/ DWORD dwSpeed;
  /*0x04*/ BYTE  _unmapped04[0x0C];
  /*0x10*/ DWORD dwSfxVolume;
  /*0x14*/ BYTE  _unmapped14[0x10];
};

static_assert(sizeof(optionsblk_t)                 == 0x24, "optionsblk_t size          option_dlg_alloc: movdqu 16B + movdqu 16B + mov dword 4B");
static_assert(offsetof(optionsblk_t, dwSpeed)      == 0x00, "optionsblk_t::dwSpeed      option_dlg_cmd: mov ecx, dword ptr [0x6D3130]");
static_assert(offsetof(optionsblk_t, dwSfxVolume)  == 0x10, "optionsblk_t::dwSfxVolume  option_dlg_cmd: mov ecx, dword ptr [0x6D3140]");

// sgOptions -- the same storage as gOptions, seen as the whole block.
#ifdef OC_DLL_MODE
#define sgOptions (*reinterpret_cast<optionsblk_t *>(OC_VA(0x6D3130u)))
#else
extern optionsblk_t sgOptions;
#endif

// The 0x28-byte heap block option_dlg_alloc makes: a copy of the options block,
// then the kind byte at +0x26. +0x24 and +0x27 are never touched.
struct optdlg_save_t {
  /*0x00*/ optionsblk_t options;
  /*0x24*/ BYTE _unmapped24[2];
  /*0x26*/ BYTE bKind;
  /*0x27*/ BYTE _unmapped27[1];
};

static_assert(sizeof(optdlg_save_t)             == 0x28, "optdlg_save_t size      option_dlg_alloc: push 28h / call SMemAlloc");
static_assert(offsetof(optdlg_save_t, options)  == 0x00, "optdlg_save_t::options  option_dlg_alloc: movdqu xmmword ptr [eax], xmm0");
static_assert(offsetof(optdlg_save_t, bKind)    == 0x26, "optdlg_save_t::bKind    option_dlg_alloc: mov byte ptr [eax+26h], 5h");

// The 0x110-byte descriptor opts_init builds on the stack and hands to
// effects_init in EDX: a kind, a count, up to 0x100 palette indices, and two
// more values whose meaning lives in effects_init.
struct effectsdesc_t {
  /*0x000*/ DWORD dwKind;
  /*0x004*/ DWORD dwCount;
  /*0x008*/ BYTE  bIndex[0x100];
  /*0x108*/ DWORD dw108;
  /*0x10C*/ BYTE  b10C;
  /*0x10D*/ BYTE  _unmapped10D[3];
};

static_assert(sizeof(effectsdesc_t)               == 0x110, "effectsdesc_t size        opts_init: push 110h / call memset");
static_assert(offsetof(effectsdesc_t, dwKind)     == 0x000, "effectsdesc_t::dwKind     opts_init: mov dword ptr [ebp-118h], 5h");
static_assert(offsetof(effectsdesc_t, dwCount)    == 0x004, "effectsdesc_t::dwCount    opts_init: inc dword ptr [ebp-114h]");
static_assert(offsetof(effectsdesc_t, bIndex)     == 0x008, "effectsdesc_t::bIndex     opts_init: mov byte ptr [ebp+eax*1-110h], bl");
static_assert(offsetof(effectsdesc_t, dw108)      == 0x108, "effectsdesc_t::dw108      opts_init: mov dword ptr [ebp-10h], 1h");
static_assert(offsetof(effectsdesc_t, b10C)       == 0x10C, "effectsdesc_t::b10C       opts_init: mov byte ptr [ebp-0Ch], 3Ch");

// Hyperion: what 1.17's game menu and its dialogs decide, in one place for them and for
// Hyperion's game menu (port_front_gamemenu.cpp), which reads and makes the same.

// The rules gamemenu_activate shows Save, Pause and Resume by in a multiplayer game or a replay, and
// the Quit abortmenu_update_timer captions End Mission's with there: one of the dialog's hidden
// captions, -10 Surrender, -11 Victory, -12 Quit (Draw), -13 Exit Replay.
static bool PortMenuSaveShown(void)
{
  return IsGameHost() && !IsLadderGame() && gPlayback == 0;
}

static bool PortMenuPauseShown(void)
{
  return gPlayback == 0 && player_can_pause(g_LocalHumanID) && !IsGamePaused();
}

static bool PortMenuResumeShown(void)
{
  return gPlayback == 0 && gPlayerData[g_LocalHumanID].nType == 2 && IsGamePaused();
}

static int PortAbortCaption(void)
{
  if (gPlayback)
    return -13;
  if (IsBNetGame() && sub_476F60() == 0)
    return -12;
  if (gbNationTypes[g_LocalNationID] == 3)
    return -11;
  return -10;
}

// The leaving quit_replay_respond, quit_to_menu_respond and the trainer's Leave share: for the main
// menu in single player, else by how the game ended for the player.
static void PortLeaveForMenu(void)
{
  if (!gbMultiPlayerMode) {
    gameloop_init(4);
    oc_d66A274 = oc_b536720 ? 22 : 6;
  } else if (gbNationTypes[g_LocalNationID] == 3) {
    gameloop_init(6);
  } else {
    gameloop_init(7);
  }
}

// quit_to_menu_respond's Quit Mission. The score screen says when the player left, and a skirmish
// of Hyperion's menus, left, goes to it as a defeat, which 1.17 passes in single player.
static void PortQuitMission(void)
{
  victdlg_quit();
  PortScoreLeft();
  if (!gbMultiPlayerMode && PortFrontScoreAfterLeave())
    gameloop_init(7);
  else
    PortLeaveForMenu();
}

// quit_respond's Exit Program.
static void PortExitProgram(void)
{
  victdlg_quit();
  gameloop_init(2);
}

// objective_set_greed_str's text for the game's resource target, NULL for one it has none for.
static const char *PortGreedText(void)
{
  switch (gametype_get_subtype_value()) {
    case 2500:
      return error_get_str(0x76);
    case 5000:
      return error_get_str(0x77);
    case 7500:
      return error_get_str(0x78);
    case 10000:
      return error_get_str(0x79);
  }
  return NULL;
}

// The objectives' text objective_activate picks by the game's type.
const char *PortObjectivesText(void)
{
  if (IsCTFGame())
    return error_get_str(0x7B);
  if (IsGreedGame())
    return PortGreedText();
  if (IsMeleeGame() || IsOOOGame(NULL))
    return error_get_str(0x75);
  if (IsSlaughterGame())
    return error_get_str(0x7A);
  if (IsSuddenDeathGame())
    return error_get_str(0x7C);
  return get_map_string(static_cast<WORD>(dword_5349A4[g_LocalNationID]));
}

// What the game menu and End Mission show: in single player the dialogs' own buttons (Save greyed
// too where the game cannot be saved, which 1.17 leaves to the save dialog), in a
// multiplayer game or a replay gamemenu_activate's and abortmenu_activate's.
void PortGameMenuRead(PortGameMenuGame *p)
{
  const bool bOwn = !gbMultiPlayerMode && !gPlayback;
  p->bSave = bOwn || PortMenuSaveShown();
  p->bSaveOff = !CanSaveGame();
  p->bLoad = bOwn;
  p->bPause = !bOwn && PortMenuPauseShown();
  p->bResume = !bOwn && PortMenuResumeShown();
  p->bRestart = bOwn;
  p->bExit = bOwn || !IsBNetGame();
  p->nQuit = bOwn ? GM_QUIT_MISSION : GM_QUIT_SURRENDER - 10 - PortAbortCaption();
}

// A step of Hyperion's menu, as 1.17's handlers make it: Pause and Resume leave it up; End
// Mission's three end in what the confirm's respond does, then the close, as its dlg_deactivate's.
void PortGameMenuDo(int nAction)
{
  switch (nAction) {
    case GM_DO_ENTER:
      modeless_dlg_enter();
      break;
    case GM_DO_CLOSE:
      gbDlgLevel = 0;
      modeless_dlg_shutdown(0);
      break;
    case GM_DO_PAUSE:
      send_net_cmd_PauseOn();
      break;
    case GM_DO_RESUME:
      send_net_cmd_PauseOff();
      break;
    case GM_DO_RESTART:
      send_net_cmd_RestartGame();
      gbDlgLevel = 0;
      modeless_dlg_shutdown(0);
      break;
    case GM_DO_QUIT:
      if (gPlayback)
        PortLeaveForMenu();
      else
        PortQuitMission();
      gbDlgLevel = 0;
      modeless_dlg_shutdown(0);
      break;
    case GM_DO_EXIT:
      PortExitProgram();
      gbDlgLevel = 0;
      modeless_dlg_shutdown(0);
      SNetLeagueLogout(gLocalPlayerName);
      break;
  }
}

// The bodies, in oracle VA order.

// The abort menu's activate: in a multiplayer game or a replay it hides the
// restart button and starts the countdown label; on Battle.net it hides button 3
// too and keeps the label ticking.
void abortmenu_activate(ctrl_t *pDlg) {
  if (gbMultiPlayerMode || gPlayback) {
    ctrl_hide(ctrl_find_ndx(pDlg, 1));
    abortmenu_update_timer(pDlg, 10);
    if (IsBNetGame()) {
      ctrl_hide(ctrl_find_ndx(pDlg, 3));
      // The cast makes minimap.h's nullary PFN_TIMER_FN take this two-argument
      // function.
      timer_create(pDlg, 10, 200,
                   reinterpret_cast<PFN_TIMER_FN *>(&abortmenu_update_timer));
    }
  }
}

// The abort menu's respond, a dense switch on gnDlgReturnVal: restart, quit to
// the menu, quit, or (-3) back up one dialog level.
void abortmenu_respond(ctrl_t *pDlg) {
  sgpRespondFcn = NULL;
  timer_remove_node(pDlg, 10);
  switch (gnDlgReturnVal) {
    case 1:
      sgpRespondFcn = &abortmenu_restart_respond;
      modeless_dlg_startup(sgRestartDlgTemplate, (void *)&menu_dlg_fcn);
      break;
    case 2:
      do_quit_to_menu_dlg();
      break;
    case 3:
      sgpRespondFcn = &quit_respond;
      modeless_dlg_startup(sgQuitDlgTemplate, (void *)&menu_dlg_fcn);
      break;
    case -3:
      // The level pop, as in quit_respond, written last as 1.17's table orders
      // the arms.
      if (--gbDlgLevel != 0) {
        --gbDlgLevel;
        sgpRespondFcn = &gamemenu_respond;
        modeless_dlg_startup(sgGameMenuDlgTemplate, (void *)&menu_dlg_fcn);
      } else {
        dlg_deactivate(pDlg);
      }
      break;
  }
}

// The abort menu's countdown label: one of four hidden template nodes, picked by
// game kind, lends its data pointer to node 2. The four ctrl_find_ndx calls are
// separate, as in 1.17.
void abortmenu_update_timer(ctrl_t *pDlg, int nId) {
  ctrl_t *pCtrl;
  ctrl_t *pSrc;

  pCtrl = ctrl_find_ndx(pDlg, 2);
  pSrc = ctrl_find_ndx(pDlg, PortAbortCaption());  // Hyperion: shared with Hyperion's menu
  pCtrl->pData = pSrc->pData;
  ctrl_set_update(pCtrl);
}

// The abort-menu stub: arms abortmenu_respond, then starts the dialog.
void do_abrtmenu_dlg() {
  sgpRespondFcn = &abortmenu_respond;
  modeless_dlg_startup(sgAbortMenuDlgTemplate, (void *)&menu_dlg_fcn);
}

void do_gamemenu_dlg() {
  // Hyperion: a benchmark run's Menu (the HUD's, F10) shows its overlay and asks whether to
  // stop, its way out
  if (PortBenchPlaying()) {
    PortBenchViewMenu();
    return;
  }
  sgpRespondFcn = &gamemenu_respond;
  modeless_dlg_startup(sgGameMenuDlgTemplate, (void *)&menu_dlg_fcn);
}

void do_helpmenu_dlg() {
  sgpRespondFcn = &helpmenu_respond;
  modeless_dlg_startup(sgHelpMenuDlgTemplate, (void *)&menu_dlg_fcn);
}

void do_optsmenu_dlg() {
  sgpRespondFcn = &optsmenu_respond;
  modeless_dlg_startup(sgOptsMenuDlgTemplate, (void *)&menu_dlg_fcn);
}

void do_quit_dlg() {
  sgpRespondFcn = &quit_respond;
  modeless_dlg_startup(sgQuitDlgTemplate, (void *)&menu_dlg_fcn);
}

// The quit-to-menu stub, guarded: a replay gets the quit-replay dialog.
void do_quit_to_menu_dlg() {
  if (gPlayback) {
    sgpRespondFcn = &quit_replay_respond;
    modeless_dlg_startup(sgQuitReplayDlgTemplate, (void *)&menu_dlg_fcn);
  } else {
    sgpRespondFcn = &quit_to_menu_respond;
    modeless_dlg_startup(sgQuitToMenuDlgTemplate, (void *)&menu_dlg_fcn);
  }
}

// Draws one horizontal band of a dialog frame: the left cap at x = 0 with frame
// nArg2, middle tiles with nArg2 + 1 while they fit, and the right cap with
// nArg2 + 2. SetRect is the USER32 import through the IAT slot at 0x4EB630. The
// band's frame walks the parameter itself, as in 1.17.
void sub_47B3C0(intptr_t nArg0, int nArg1, int nArg2, void *pArg3) {
  ctrl_t *pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  GROUP *pGrp = reinterpret_cast<GROUP *>(pArg3);
  rect32_t rc;
  FRAME frame;
  grhsize_t size;
  int x;

  SetRect(reinterpret_cast<LPRECT>(&rc), 0, 0, pGrp->wMaxWidth,
          pGrp->wMaxHeight);
  grh_get_header(pGrp, nArg2, &frame, &size);
  rle_draw_normal(0, nArg1, &frame, &rc, 0);
  ++nArg2;
  for (x = pGrp->wMaxWidth; x < ctrl_bmp(pDlg)->nWidth - pGrp->wMaxWidth;
       x += pGrp->wMaxWidth) {
    grh_get_header(pGrp, nArg2, &frame, &size);
    rle_draw_normal(x, nArg1, &frame, &rc, 0);
  }
  ++nArg2;
  grh_get_header(pGrp, nArg2, &frame, &size);
  rle_draw_normal(x, nArg1, &frame, &rc, 0);
}

// This TU's exit handler, registered with app_add_exit by sub_47B760. Both
// stores are inside the guard.
void option_dlg_free() {
  if (sgpTileGrp != NULL) {
    mem_free(sgpTileGrp, "opt_dlgs.cpp", 58);
    sgpTileGrp = NULL;
    sgszTileGrpName[0] = '\0';
  }
}

// The in-game menu's activate: hides restart, grays save unless this is a live
// host game, then shows pause or resume.
void gamemenu_activate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;

  ctrl_hide(ctrl_find_ndx(pDlg, 2));
  // Hyperion: the rules shared with Hyperion's game menu
  if (PortMenuSaveShown()) {
    pCtrl = ctrl_find_ndx(pDlg, 1);
    ctrl_set_gray(pCtrl, !CanSaveGame());
  } else {
    ctrl_hide(ctrl_find_ndx(pDlg, 1));
  }
  pCtrl = ctrl_find_ndx(pDlg, 7);
  ctrl_showhide(pCtrl, PortMenuPauseShown());
  pCtrl = ctrl_find_ndx(pDlg, 8);
  ctrl_showhide(pCtrl, PortMenuResumeShown());
  if (gPlayback == 0)
    timer_create(pDlg, 1, 200, reinterpret_cast<PFN_TIMER_FN *>(&gamemenu_timer_fn));
}

// The game menu's respond, a dense switch on gnDlgReturnVal from -3 to 8. Cases
// 7 and 8 send the pause and unpause commands and keep the slot armed.
void gamemenu_respond(ctrl_t *pDlg) {
  sgpRespondFcn = NULL;
  switch (gnDlgReturnVal) {
    case 1:
      do_multi_save_dlg();
      break;
    case 2:
      do_multi_load_dlg();
      break;
    case 3:
      do_optsmenu_dlg();
      break;
    case 4:
      do_helpmenu_dlg();
      break;
    case 5:
      modeless_dlg_startup(sgObjectiveDlgTemplate, (void *)&objective_dlg_fcn);
      break;
    case 6:
      do_abrtmenu_dlg();
      break;
    case 7:
      send_net_cmd_PauseOn();
      sgpRespondFcn = &gamemenu_respond;
      break;
    case 8:
      send_net_cmd_PauseOff();
      sgpRespondFcn = &gamemenu_respond;
      break;
    case -3:
      gbDlgLevel = 0;
      dlg_deactivate(pDlg);
      break;
  }
}

// Three enable and disable passes over the game menu, each ctrl_find_ndx a
// statement of its own so it runs before the predicate, as in 1.17.
void gamemenu_timer_fn(ctrl_t *pDlg) {
  ctrl_t *pCtrl;

  pCtrl = ctrl_find_ndx(pDlg, 7);
  ctrl_showhide(pCtrl, player_can_pause(g_LocalHumanID) && !IsGamePaused());
  pCtrl = ctrl_find_ndx(pDlg, 8);
  ctrl_showhide(pCtrl, IsGamePaused());
  pCtrl = ctrl_find_ndx(pDlg, 1);
  ctrl_set_gray(pCtrl, !CanSaveGame());
}

// The help menu's respond: tips, help, or (-3) back up one level. do_tips_dlg
// takes a null argument in ECX.
void helpmenu_respond(ctrl_t *pDlg) {
  sgpRespondFcn = NULL;
  switch (gnDlgReturnVal) {
    case 2:
      do_tips_dlg(0);
      break;
    case 1:
      do_help_dlg();
      break;
    case -3:
      if (--gbDlgLevel != 0) {
        --gbDlgLevel;
        sgpRespondFcn = &gamemenu_respond;
        modeless_dlg_startup(sgGameMenuDlgTemplate, (void *)&menu_dlg_fcn);
      } else {
        dlg_deactivate(pDlg);
      }
      break;
  }
}

// Loads dlgs\tile.grp, or the name the caller hands it, keeping one GRP and the
// name it was loaded under cached in .data. The second test of sgpTileGrp is
// an if of its own, as in 1.17.
void sub_47B760(const char *pszFile) {
  if (sgpTileGrp == NULL)
    app_add_exit(&option_dlg_free);
  if (sgpTileGrp != NULL && _stricmp(sgszTileGrpName, pszFile) == 0)
    return;
  if (sgpTileGrp != NULL) {
    mem_free(sgpTileGrp, "opt_dlgs.cpp", 58);
    sgpTileGrp = NULL;
    sgszTileGrpName[0] = '\0';
  }
  sgpTileGrp = ReadGroupFile(pszFile, 0, "opt_dlgs.cpp", 80);
  strcpy(sgszTileGrpName, pszFile);
}

// The F10 menu's shared activate: builds the dialog chrome, then runs four
// independent ifs on which respond is armed. The tile GRP is read into a local
// before the flags are written, so the load comes first, as in 1.17.
void menu_activate(ctrl_t *pDlg)
{
  dfl_load_bmp(pDlg, NULL);
  GROUP *pGrp = sgpTileGrp;
  pDlg->dwFlags |= 0x10040000;
  sub_4CA820(0x3C, sub_47C110(reinterpret_cast<intptr_t>(pGrp), 4, 0, 0), 0);
  ctrl_set_evt_fcns(pDlg, sgMenuEvtFcnTbl, 0x20);
  set_size_enter(pDlg, 4, 0x20000000);
  set_size_escape(pDlg, 4, 0x20000000);
  if (sgpRespondFcn == &abortmenu_respond)
    abortmenu_activate(pDlg);
  if (sgpRespondFcn == &gamemenu_respond && (gbMultiPlayerMode || gPlayback))
    gamemenu_activate(pDlg);
  if (sgpRespondFcn == &quit_to_menu_respond && nullsub___shared_() == 0)
    ctrl_hide(ctrl_find_ndx(pDlg, 1));
  if (sgpRespondFcn == &optsmenu_respond && !gbMultiPlayerMode)
    ctrl_hide(ctrl_find_ndx(pDlg, 4));
  ++gbDlgLevel;
}

// The dialog event dispatch, a dense switch on the event id: case 0 activates
// and falls through to the tail, case 1 shuts the dialog and returns 1, case 2
// fires the deferred action.
BOOL menu_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        menu_activate(pCtrl);
        break;
      case 1:
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
      case 2:
        sgpRespondFcn(pCtrl);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The menu's deactivate: the shutdown, then a tail jump to modeless_opt_dlg.
BOOL modeless_menu_deactivate(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  modeless_dlg_shutdown(0);
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The mission-objectives dialog's activate: menu_activate's chrome, then a
// game-type chain that picks the objective text for control 2. The melee and
// OOO arms are two else-ifs with the same body, as in 1.17, and IsOOOGame's
// NULL means the current game header.
void objective_activate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl;

  dfl_load_bmp(pDlg, NULL);
  GROUP *pGrp = sgpTileGrp;
  pDlg->dwFlags |= 0x10040000;
  sub_4CA820(0x3C, sub_47C110(reinterpret_cast<intptr_t>(pGrp), 4, 0, 0), 0);
  set_size_escape(pDlg, 4, 0x20000000);
  pCtrl = ctrl_find_ndx(pDlg, 2);
  // Hyperion: the text PortObjectivesText picks, shared with Hyperion's game menu;
  // a Greed target with none leaves the control's, as objective_set_greed_str did
  const char *pszText = PortObjectivesText();
  if (pszText != NULL || !IsGreedGame())
    pCtrl->pData = const_cast<char *>(pszText);
  ++gbDlgLevel;
}

// The objectives dialog's command: back up one level, or close. It takes the
// dialog, which it passes on to dlg_deactivate in ECX.
void objective_cmd(ctrl_t *pDlg) {
  if (--gbDlgLevel != 0) {
    --gbDlgLevel;
    sgpRespondFcn = &gamemenu_respond;
    modeless_dlg_startup(sgGameMenuDlgTemplate, (void *)&menu_dlg_fcn);
  } else {
    dlg_deactivate(pDlg);
  }
}

// The objectives dialog's event dispatch, menu_dlg_fcn's shape with
// objective_cmd as case 2.
BOOL objective_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent) {
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0:
        objective_activate(pCtrl);
        break;
      case 1:
        modeless_dlg_shutdown(0);
        modeless_opt_dlg(pCtrl, pEvent);
        return TRUE;
      case 2:
        objective_cmd(pCtrl);
        return TRUE;
    }
  }
  return modeless_opt_dlg(pCtrl, pEvent);
}

// The greed objective's text for the game's resource target, a switch 1.17
// lowers as a binary search.
void objective_set_greed_str(ctrl_t *pDlg) {
  // Hyperion: the text PortGreedText picks, shared with Hyperion's game menu
  const char *psz = PortGreedText();
  if (psz != NULL)
    pDlg->pData = (void *)psz;
}

// Builds a control's dialog-frame bitmap out of the tile GRP: rounds the
// control out to whole tiles, allocates and clears the bitmap, then lays the
// top band, the middle bands and the bottom band. The default name is escaped
// in source; an unescaped \t compiles silently to a tab.
void dfl_load_bmp(ctrl_t *pDlg, const char *pszFile) {
  void *pOldBmp = _bmp_current;
  GROUP *pGrp;
  short nW;
  int nRem;
  int y;

  sub_47B760(pszFile != NULL ? pszFile : "dlgs\\tile.grp");
  pGrp = sgpTileGrp;

  // The bitmap is reached through pDlg each time, as in 1.17.
  nW = ctrl_bmp(pDlg)->nWidth;
  nRem = nW % pGrp->wMaxWidth;
  if (nRem != 0)
    ctrl_bmp(pDlg)->nWidth = static_cast<short>(nW + (pGrp->wMaxWidth - nRem));
  // No local for the height, unlike the width: 1.17 rounds it in place.
  nRem = ctrl_bmp(pDlg)->nHeight % pGrp->wMaxHeight;
  if (nRem != 0)
    ctrl_bmp(pDlg)->nHeight += static_cast<short>(pGrp->wMaxHeight - nRem);

  bmp_create(ctrl_bmp(pDlg), "opt_dlgs.cpp", 212);
  _bmp_current = ctrl_bmp(pDlg);
  bmp_clear(0);

  sub_47B3C0(reinterpret_cast<intptr_t>(pDlg), 0, 0, pGrp);
  for (y = pGrp->wMaxHeight; y < ctrl_bmp(pDlg)->nHeight - pGrp->wMaxHeight;
       y += pGrp->wMaxHeight) {
    sub_47B3C0(reinterpret_cast<intptr_t>(pDlg), y, 3, pGrp);
  }
  sub_47B3C0(reinterpret_cast<intptr_t>(pDlg), y, 6, pGrp);

  _bmp_current = pOldBmp;
}

// Tears down the effects once opts_init has set them up.
void opts_free() {
  if (sgbOptsInited) {
    effects_free();
    sgbOptsInited = 0;
  }
}

// Frees the dialog's +0x26 block (owner line 990), then shuts the dialog. The
// shutdown's argument is one byte.
void opt_dlg_generic_close(ctrl_t *pDlg) {
  SMemFree(*opt_dlg_data26(pDlg), "opt_dlgs.cpp", 990, 0);
  *opt_dlg_data26(pDlg) = NULL;
  modeless_dlg_shutdown(0);
}

// Builds the effects descriptor with every colour the palette cycles, hands it
// to effects_init with the game palette, sets a 60/60/60 render type and raises
// the module's flag.
void opts_init() {
  effectsdesc_t desc;
  rendertype_t rt;
  int i;

  memset(&desc, 0, sizeof(desc));
  desc.dwKind = 5;
  desc.dw108 = 1;
  desc.b10C = 60;
  for (i = 0; i < 256; ++i) {
    if (is_color_cycled(i)) {
      desc.bIndex[desc.dwCount] = static_cast<BYTE>(i);
      ++desc.dwCount;
    }
  }
  effects_init(sgGamePalette_g, &desc);

  rt.w0 = 60;
  rt.w2 = 60;
  rt.w4 = 60;
  sub_4C4D50(&rt);

  sgbOptsInited = 1;
}

// Sets the dialog's flags and its blend, as menu_activate does.
void opt_dlg_set_blend(ctrl_t *pDlg) {
  pDlg->dwFlags |= 0x10040000;
  sub_4CA820(0x3C, sub_47C110(reinterpret_cast<intptr_t>(sgpTileGrp), 4, 0, 0), 0);
}

// The tile GRP, as replay.cpp reads it.
intptr_t opt_get_tile_grp() {
  return reinterpret_cast<intptr_t>(sgpTileGrp);
}

#ifndef OC_PAYLOAD
// Empty: option_dlg_alloc and option_dlg_cmd were once held here.
#endif  // OC_PAYLOAD

// The options dialog's save block: SMemAlloc's own result, 0x28 bytes, owner
// line 976, holding a copy of the options block and kind 5.
intptr_t option_dlg_alloc() {
  optdlg_save_t *pSave = reinterpret_cast<optdlg_save_t *>(
      SMemAlloc(sizeof(optdlg_save_t), "opt_dlgs.cpp", 976, 0));

  pSave->options = sgOptions;
  pSave->bKind = 5;
  return reinterpret_cast<intptr_t>(pSave);
}

// The options dialog's command. ECX is the dialog, whose +0x26 slot holds what
// option_dlg_alloc made. Cancel (-3) restores the saved options, all but the
// speed in a replay; then the handlers, sound options and volume are applied.
// The speed is saved through sgOptions, not gOptions (see optionsblk_t).
void option_dlg_cmd(void *pArg0) {
  const optionsblk_t *pSaved;
  DWORD dwSpeed;

  if (gnDlgReturnVal == -3) {
    // Hyperion: the dialog's +0x26 slot, through its window.
    pSaved = *reinterpret_cast<const optionsblk_t *const *>(
        &static_cast<ctrl_t *>(pArg0)->_unmapped24[2]);
    dwSpeed = sgOptions.dwSpeed;
    sgOptions = *pSaved;
    if (gPlayback)
      sgOptions.dwSpeed = dwSpeed;
  } else {
    gbDlgLevel = 1;
  }
  game_reset_handlers();
  snd_set_options(&sgOptions);
  if (is_sfx_enabled())
    set_digital_volume(sgSndVolTbl_g[sgOptions.dwSfxVolume - 1]);
  else
    set_digital_volume(-10000);
}

// The options menu's respond, a dense switch on gnDlgReturnVal: the speed,
// sound, video and network dialogs, or (-3) back up one level.
void optsmenu_respond(ctrl_t *pDlg) {
  sgpRespondFcn = NULL;
  switch (gnDlgReturnVal) {
    case 1:
      do_speed_dlg();
      break;
    case 2:
      do_sound_dlg();
      break;
    case 3:
      do_video_dlg();
      break;
    case 4:
      do_net_dlg();
      break;
    case -3:
      if (--gbDlgLevel != 0) {
        --gbDlgLevel;
        sgpRespondFcn = &gamemenu_respond;
        modeless_dlg_startup(sgGameMenuDlgTemplate, (void *)&menu_dlg_fcn);
      } else {
        dlg_deactivate(pDlg);
      }
      break;
  }
}

// The quit-replay respond. Quit (-2) leaves the game, for the main menu in
// single player; anything else backs up one level. The two gameloop_init calls
// and the one dlg_deactivate after the chain are 1.17's shape.
void quit_replay_respond(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -2) {
    sgpRespondFcn = NULL;
    PortLeaveForMenu();  // Hyperion: shared with Hyperion's game menu
    dlg_deactivate(pDlg);
  } else {
    sgpRespondFcn = NULL;
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      sgpRespondFcn = &abortmenu_respond;
      modeless_dlg_startup(sgAbortMenuDlgTemplate, (void *)&menu_dlg_fcn);
    } else {
      dlg_deactivate(pDlg);
    }
  }
}

// The quit respond. Quit (-2) ends the game and logs out of the league;
// anything else backs up one level. The slot is cleared before the branch.
void quit_respond(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -2) {
    sgpRespondFcn = NULL;
    PortExitProgram();  // Hyperion: shared with Hyperion's game menu
    dlg_deactivate(pDlg);
    SNetLeagueLogout(gLocalPlayerName);
  } else {
    sgpRespondFcn = NULL;
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      sgpRespondFcn = &abortmenu_respond;
      modeless_dlg_startup(sgAbortMenuDlgTemplate, (void *)&menu_dlg_fcn);
    } else {
      dlg_deactivate(pDlg);
    }
  }
}

// Hyperion: the trainer's Leave (port_trainer.cpp): quit_to_menu_respond's
// leaving, with no dialog to close.
void PortQuitToMenu(void) {
  victdlg_quit();
  PortLeaveForMenu();
}

void quit_to_menu_respond(ctrl_t *pDlg) {
  LONG nRet = gnDlgReturnVal;

  if (nRet == -2) {
    sgpRespondFcn = NULL;
    // Hyperion: the leaving, with the score screen's, shared with Hyperion's game
    // menu
    PortQuitMission();
    dlg_deactivate(pDlg);
  } else if (nRet == 1) {
    sgpRespondFcn = NULL;
    voice_squelch();
    dlg_deactivate(pDlg);
    gbDlgLevel = 0;
  } else {
    sgpRespondFcn = NULL;
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      sgpRespondFcn = &abortmenu_respond;
      modeless_dlg_startup(sgAbortMenuDlgTemplate, (void *)&menu_dlg_fcn);
    } else {
      dlg_deactivate(pDlg);
    }
  }
}

// The restart respond: restart the game, or (-3) back up one level. The load of
// gnDlgReturnVal comes before the slot store, as in 1.17.
void abortmenu_restart_respond(ctrl_t *pDlg) {
  if (gnDlgReturnVal == -3) {
    sgpRespondFcn = NULL;
    if (--gbDlgLevel != 0) {
      --gbDlgLevel;
      sgpRespondFcn = &abortmenu_respond;
      modeless_dlg_startup(sgAbortMenuDlgTemplate, (void *)&menu_dlg_fcn);
    } else {
      dlg_deactivate(pDlg);
    }
  } else {
    sgpRespondFcn = NULL;
    send_net_cmd_RestartGame();
    dlg_deactivate(pDlg);
  }
}

// The GRP pixel hit-test: is (x, y) opaque in frame n of this group? FRAME+0x01
// is the Y offset and FRAME+0x04 the pixel-data offset from the GROUP base, both
// reached by cast; the frame index is read 16 bits wide. In a run's control
// byte, bit 7 is a transparent run and bit 6 one repeated byte.
BYTE sub_47C110(intptr_t nArg0, short nArg1, int nArg2, int nArg3) {
  GROUP *pGroup = reinterpret_cast<GROUP *>(nArg0);
  FRAME *pFrame = &pGroup->frames[static_cast<WORD>(nArg1)];
  const BYTE *pFrameBytes = reinterpret_cast<const BYTE *>(pFrame);
  int x = nArg2 - pFrame->bOffsetX;
  int y = nArg3 - pFrameBytes[1];

  const BYTE *pRle;
  const BYTE *p;

  // The four guards are one || with one early return, as 1.17's layout needs.
  if (x < 0 || y < 0 || x >= pFrame->bWidth || y >= pFrame->bHeight)
    return 0;

  // FRAME+4 is the frame's pixel-data offset, relative to the GROUP base,
  // with bit 31 masked off.
#if defined(_WIN64) || defined(__LP64__)
  // Hyperion: the loader made pData the pointer (PortGroupNative).
  pRle = pFrame->pData;
#else
  pRle = reinterpret_cast<const BYTE *>(pGroup)
       + (*reinterpret_cast<const DWORD *>(pFrameBytes + 4) & 0x7FFFFFFF);
#endif
  // The row table: one WORD per scanline, itself relative to pRle.
  p = pRle + reinterpret_cast<const WORD *>(pRle)[y];

  {
    do {
      // bCtl is signed and nRun a BYTE, as in 1.17.
      char bCtl = static_cast<char>(*p++);
      BYTE nRun = static_cast<BYTE>(bCtl & 0x3F);

      if (x < nRun) {
        if (bCtl < 0)
          break;                 // a transparent run covers this pixel
        if (bCtl & 0x40)
          return *p;             // one repeated byte
        return p[x];             // a literal run
      }
      x -= nRun;
      if (bCtl < 0)
        continue;                // transparent: the run carries no data
      if (bCtl & 0x40)
        ++p;
      else
        p += nRun;
    } while (x >= 0);
  }
  return 0;
}

