// gluScore.cpp -- the end-of-game score screen and its battle.net glue.
// Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/playertables.h"  // Hyperion: player slots past 11
#include "../h/gluScore.h"
#include "../h/bmp.h"         // bmp_t
#include "../h/dlg_ctrl.h"    // ctrl_find_ndx
#include "../h/CUnitColor.h"  // get_slot_mapping
#include "../h/dlg_draw.h"    // draw_bmp_effect, draw_ctrl_text
#include "../h/dlg_flc.h"     // ctrl_btnglow_flc
#include "../h/forces.h"      // GetForceName
#include "../h/gametype.h"    // IsScenarioGame, team_get_slot_captain
#include "../h/glues.h"       // glue_init_screen_dlg, glues_transition_in
#include "../h/gluRdy.h"     // glues_transition_out
#include "../h/net_cmds.h"    // PlayerData, error_get_str
#include "../h/minimap.h"     // timer_create, timer_remove_node
#include "../h/net_time.h"    // sub_47A780
#include "../h/replay.h"      // do_replay_dlg
#include "../h/snd.h"         // stop_digital
#include "../h/Teams.h"       // IsTeamGame, GetTeamName
#include "../h/Storm.h"       // SMemFree
#include "../h/victory.h"     // psnprintf
#include "../h/gds_grp2bmpx.h" // grp_to_bmpx
#include "../h/SystemSurvey.h" // get_printable_string
#include "../h/dlg_dlg.h"     // dlg_load, dlg_run_modal
#include "../h/gamedata.h"    // ReadGroupFile
#include "../h/uiSingle.h"    // sub_4BB790, sub_4BB970
#include "../h/sha1.h"        // SHA1_Init, SHA1_Update, SHA1_Final
#include "../h/net_glue.h"    // Hyperion: NetworkShutdown
#include "../port/lan/port_lanlobby.h"  // Hyperion: PortLanBackToFront
#include "../port/front/port_front.h"   // Hyperion: PortFrontScore
#include "../port/gameplay/port_score.h"  // Hyperion: ScoreEnd
#include <string.h>
#include <time.h>
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// 0x66B414 gbWasDemoMode_g -- set while the game played was a demo, and
// cleared once the score screen has read it.
#ifndef OC_GBWASDEMOMODE_G
#define OC_GBWASDEMOMODE_G
#ifdef OC_DLL_MODE
#define gbWasDemoMode_g (*reinterpret_cast<int *>(OC_VA(0x66B414u)))
#else
extern int gbWasDemoMode_g;
#endif
#endif

// 0x66B410 gbWasLoaded_g -- gluscore_set_loaded stores its whole ECX here, and
// the save-replay button's guard reads it.
#ifndef OC_GBWASLOADED_G
#define OC_GBWASLOADED_G
#ifdef OC_DLL_MODE
#define gbWasLoaded_g (*reinterpret_cast<int *>(OC_VA(0x66B410u)))
#else
extern int gbWasLoaded_g;
#endif
#endif

// 0x536722 -- a BYTE per player, written by sub_461B70 and read back by
// sub_4628C0 and sub_462C50 as a small index into the bar frames and into
// 0x66CA5C. It has no name of its own and its extent is not proven.
#ifndef OC_GBSLOTREMAP_G
#define OC_GBSLOTREMAP_G
#ifdef OC_DLL_MODE
#define gbSlotRemap_g (reinterpret_cast<BYTE *>(OC_VA(0x536722u)))
#else
extern BYTE gbSlotRemap_g[12];
#endif
#endif

// 0x5290C4 glGameCounts_g -- a DWORD[28][12]: the row index is (offset / 48)
// and the player is the inner index.
#ifndef OC_GLGAMECOUNTS_G
#define OC_GLGAMECOUNTS_G
#ifdef OC_DLL_MODE
#define glGameCounts_g (reinterpret_cast<DWORD (*)[12]>(OC_VA(0x5290C4u)))
#else
extern PlayerGameCountsGrid glGameCounts_g;
#endif
#endif

// 0x526430 and 0x526460 -- the two int[12] arrays directly above gPlayerGas.
// sub_462BB0 subtracts the current minerals and gas from their sum and clamps
// the result at zero, which is what makes them the cumulative totals.
// Hyperion: 0x526430 is the gas and 0x526460 the minerals, as
// CUnit::DepositInventory adds them and gametype_init_resources seeds the
// mineral one; the two names were the other way round (docs/replay-test.md).
#ifndef OC_GPLAYERGASGATHERED_G
#define OC_GPLAYERGASGATHERED_G
#ifdef OC_DLL_MODE
#define gPlayerGasGathered_g (reinterpret_cast<int *>(OC_VA(0x526430u)))
#else
extern PlayerGasGatheredTable gPlayerGasGathered_g;
#endif
#endif

#ifndef OC_GPLAYERMINERALSGATHERED_G
#define OC_GPLAYERMINERALSGATHERED_G
#ifdef OC_DLL_MODE
#define gPlayerMineralsGathered_g (reinterpret_cast<int *>(OC_VA(0x526460u)))
#else
extern PlayerMineralsGatheredTable gPlayerMineralsGathered_g;
#endif
#endif

// 0x5084D0 sScoreEvtFcnTbl_g and 0x5085C4 sScoreAnims_g.
// gluscore_dlg_fcn passes 0xF4 as the table's size in bytes, which is 61
// entries and puts its end exactly at the anims table. The anims table is
// address-taken only, so its extent is not derived.
#ifndef OC_SSCOREEVTFCNTBL_G
#define OC_SSCOREEVTFCNTBL_G
#ifdef OC_DLL_MODE
#define sScoreEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x5084D0u)))
#else
extern PFN_CTRL_EVT sScoreEvtFcnTbl_g[61];
#endif
#endif

#ifndef OC_SSCOREANIMS_G
#define OC_SSCOREANIMS_G
#ifdef OC_DLL_MODE
#define sScoreAnims_g (reinterpret_cast<char *>(OC_VA(0x5085C4u)))
#else
extern char sScoreAnims_g[];
#endif
#endif

// 0x66C950 -- gluScore.cpp .data, passed whole as do_replay_dlg's EDX.
#ifndef OC_DWORD_66C950
#define OC_DWORD_66C950
#ifdef OC_DLL_MODE
#define dword_66C950 (*reinterpret_cast<int *>(OC_VA(0x66C950u)))
#else
extern int dword_66C950;
#endif
#endif

// 0x526524 -- a WORD (`cmp word ptr [0x526524], 0h`), two bytes below
// TrigAct.cpp's byte_526526, with no name of its own.
#ifndef OC_WORD_526524
#define OC_WORD_526524
#ifdef OC_DLL_MODE
#define word_526524 (*reinterpret_cast<WORD *>(OC_VA(0x526524u)))
#else
extern WORD word_526524;
#endif
#endif

// The one Win32 import this TU reaches: `push 3E8h / call dword ptr
// [0x4EB2F8]`, spelled as event_Win.cpp and gluModem.cpp spell it.
extern "C" __declspec(dllimport) void __stdcall Sleep(DWORD dwMilliseconds);

// gds_font.cpp 0x4CDF20, spelled as CtxtHelp.cpp and dlg_draw.cpp spell it.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// scoreslot_t -- one of the eight slot records at 0x66C710. Everything but the
// two strings is reached by displacement off a 72-byte stride.
struct scoreslot_t {
  // +0x00 the player name: sub_4626E0 hands the record's own address to
  // ctrl_set_text, and PlayerData::szName is 25 bytes.
  /*0x00*/ char szName[0x19];
  // +0x19 a second caption, passed to get_printable_string with a 0x1E limit.
  /*0x19*/ char szLabel[0x1F];
  // +0x38 the player number. sub_4629F0 loads it as the fill callback's EDX
  // and sub_4628C0 hands it to get_slot_mapping.
  /*0x38*/ LONG nPlayer;
  // +0x3C a storm id: sub_4623C0 indexes dword_66CA70 with it and compares it
  // against g_LocalStormID.
  /*0x3C*/ LONG nStormId;
  // +0x40 the slot type, tested against 1, 2, 0x0A and 0x0B.
  /*0x40*/ LONG nType;
  // +0x44 a BYTE biased by 3 into grp_to_bmpx's frame (sub_462C50).
  /*0x44*/ BYTE bRace;
  /*0x45*/ BYTE _unmapped45[3];
};

static_assert(sizeof(scoreslot_t) == 0x48, "scoreslot_t      stride: lea ecx,[ecx+ecx*8] with a scale of 8");
static_assert(offsetof(scoreslot_t, szName) == 0x00, "scoreslot_t::szName   sub_4626E0: lea edx, dword ptr [ecx*8+66C710h]");
static_assert(offsetof(scoreslot_t, szLabel) == 0x19, "scoreslot_t::szLabel  sub_462C50: lea edi, dword ptr [eax*8+66C729h]");
static_assert(offsetof(scoreslot_t, nPlayer) == 0x38, "scoreslot_t::nPlayer  sub_4628C0: mov ecx, dword ptr [ecx*8+66C748h]");
static_assert(offsetof(scoreslot_t, nStormId) == 0x3C, "scoreslot_t::nStormId sub_4623C0: mov ecx, dword ptr [ecx*8+66C74Ch]");
static_assert(offsetof(scoreslot_t, nType) == 0x40, "scoreslot_t::nType    sub_4623C0: cmp dword ptr [ecx*8+66C750h], 1h");
static_assert(offsetof(scoreslot_t, bRace) == 0x44, "scoreslot_t::bRace    sub_462C50: mov bl, byte ptr [eax*8+66C754h]");

// 0x66C710 -- the eight slot records the score screen reports on. The stride
// is 72 (`lea ecx, [ecx+ecx*8]` then a scale of 8) and the eighth record ends
// exactly at 0x66C950, so the extent is exact.
#ifndef OC_SGSCORESLOTS_G
#define OC_SGSCORESLOTS_G
#ifdef OC_DLL_MODE
#define sgScoreSlots_g (reinterpret_cast<scoreslot_t *>(OC_VA(0x66C710u)))
#else
extern scoreslot_t sgScoreSlots_g[8];
#endif
#endif

// 0x66CA70 -- a DWORD per storm id, holding 1 for a winner and 2 for a loser;
// the extent is not proven. 0x66CA90 carries the same two values for the local
// player. Both names are positional.
#ifndef OC_DWORD_66CA70
#define OC_DWORD_66CA70
#ifdef OC_DLL_MODE
#define dword_66CA70 (reinterpret_cast<int *>(OC_VA(0x66CA70u)))
#else
extern int dword_66CA70[8];
#endif
#endif

#ifndef OC_DWORD_66CA90
#define OC_DWORD_66CA90
#ifdef OC_DLL_MODE
#define dword_66CA90 (*reinterpret_cast<int *>(OC_VA(0x66CA90u)))
#else
extern int dword_66CA90;
#endif
#endif

// 0x66C958 -- the score screen's bitmap directory, filled from off_508454 and
// ending exactly at byte_66CA5C, so the 0x104 extent is exact and is also the
// size of the stack buffer gluscore_dlg_activate builds from it.
#ifndef OC_BYTE_66C958
#define OC_BYTE_66C958
#ifdef OC_DLL_MODE
#define byte_66C958 (reinterpret_cast<char *>(OC_VA(0x66C958u)))
#else
extern char byte_66C958[0x104] OC_UNALIGNED;
#endif
#endif

// The three cached screen bitmaps, each with its own "is loaded" flag: one
// bmp_t at the named address and the flag one dword below it.
#ifndef OC_DWORD_66CA98
#define OC_DWORD_66CA98
#ifdef OC_DLL_MODE
#define dword_66CA98 (*reinterpret_cast<int *>(OC_VA(0x66CA98u)))
#define sgGluScoreboxBmp_g (*reinterpret_cast<bmp_t *>(OC_VA(0x66CA9Cu)))
#define dword_66CAA4 (*reinterpret_cast<int *>(OC_VA(0x66CAA4u)))
#define sgGluScoreInsetBmp_g (*reinterpret_cast<bmp_t *>(OC_VA(0x66CAA8u)))
#define dword_66CAB0 (*reinterpret_cast<int *>(OC_VA(0x66CAB0u)))
#define szGluScoreTabBmp_g (*reinterpret_cast<bmp_t *>(OC_VA(0x66CAB4u)))
#else
extern int dword_66CA98;
extern bmp_t sgGluScoreboxBmp_g;
extern int dword_66CAA4;
extern bmp_t sgGluScoreInsetBmp_g;
extern int dword_66CAB0;
extern bmp_t szGluScoreTabBmp_g;
#endif
#endif

// The bmp_t a ctrl_t carries at +0x0C, reached through dialog.h's
// `_unmapped0C` window. A macro rather than a function so no out-of-line
// COMDAT is left behind, the same reason dlg_draw.cpp gives.
#define ctrl_bmp(p) (reinterpret_cast<bmp_t *>(&(p)->_unmapped0C[0]))

// The scorerow_t a ctrl_t carries at +0x26, inside dialog.h's `_unmapped24[6]`
// window: sub_4626E0 stores the row there and every reader dereferences it.
#define ctrl_row(p) (*reinterpret_cast<scorerow_t **>(&(p)->_unmapped24[2]))

// 0x66B430 -- the eight score rows, 0x25C each; gluScore_tab_ctrl_updatetab
// memsets exactly 0x12E0 bytes here. 0x66CA6C holds how many of them are live.
// 0x66B420 dword_66B420 -- the selected tab's position among the four, 0..3.
#ifndef OC_DWORD_66B420
#define OC_DWORD_66B420
#ifdef OC_DLL_MODE
#define dword_66B420 (*reinterpret_cast<LONG *>(OC_VA(0x66B420u)))
#else
extern LONG dword_66B420;
#endif
#endif

// 0x66B424 qword_66B424 -- named as one quadword, used as three longs, one per
// column (sub_4628C0 indexes it by the bar's column).
#ifndef OC_QWORD_66B424
#define OC_QWORD_66B424
#ifdef OC_DLL_MODE
#define qword_66B424 (reinterpret_cast<LONG *>(OC_VA(0x66B424u)))
#else
extern LONG qword_66B424[3];
#endif
#endif

// 0x50849C sScoreStrs_g -- four tabs by three column-title string ids.
#ifndef OC_SSCORESTRS_G
#define OC_SSCORESTRS_G
#ifdef OC_DLL_MODE
#define sScoreStrs_g (reinterpret_cast<LONG (*)[3]>(OC_VA(0x50849Cu)))
#else
extern LONG sScoreStrs_g[4][3] OC_UNALIGNED;
#endif
#endif

// 0x66CA68 dword_66CA68 -- set when the local player's slot is an observer
// (type 9).
#ifndef OC_DWORD_66CA68
#define OC_DWORD_66CA68
#ifdef OC_DLL_MODE
#define dword_66CA68 (*reinterpret_cast<LONG *>(OC_VA(0x66CA68u)))
#else
extern LONG dword_66CA68;
#endif
#endif

// 0x66CA94 dword_66CA94 -- set once sub_461E00 has filled the slot records.
#ifndef OC_DWORD_66CA94
#define OC_DWORD_66CA94
#ifdef OC_DLL_MODE
#define dword_66CA94 (*reinterpret_cast<LONG *>(OC_VA(0x66CA94u)))
#else
extern LONG dword_66CA94;
#endif
#endif

// 0x66C954 dword_66C954 -- the score screen's dialog grp, swapped into
// gpCurrDlgGrp_g while a bar draws.
#ifndef OC_DWORD_66C954
#define OC_DWORD_66C954
#ifdef OC_DLL_MODE
#define dword_66C954 (*reinterpret_cast<void **>(OC_VA(0x66C954u)))
#else
extern void *dword_66C954;
#endif
#endif

#ifndef OC_SGSCOREROWS_G
#define OC_SGSCOREROWS_G
#ifdef OC_DLL_MODE
#define sgScoreRows_g (reinterpret_cast<scorerow_t *>(OC_VA(0x66B430u)))
#define dword_66CA6C (*reinterpret_cast<int *>(OC_VA(0x66CA6Cu)))
#else
extern scorerow_t sgScoreRows_g[8] OC_UNALIGNED;
extern int dword_66CA6C;
#endif
#endif

// 0x66CA5C byte_66CA5C -- a BYTE per color index, the pen a row's name box is
// outlined in. Its extent is not proven.
#ifndef OC_BYTE_66CA5C
#define OC_BYTE_66CA5C
#ifdef OC_DLL_MODE
#define byte_66CA5C (reinterpret_cast<BYTE *>(OC_VA(0x66CA5Cu)))
#else
extern BYTE byte_66CA5C[12];
#endif
#endif

// 0x50F160 gbFontAllowWrap -- gds/font.cpp's wrap flag, cleared around a
// measured draw.
#ifndef OC_GBFONTALLOWWRAP
#define OC_GBFONTALLOWWRAP
#ifdef OC_DLL_MODE
#define gbFontAllowWrap (*reinterpret_cast<BYTE *>(OC_VA(0x50F160u)))
#else
extern BYTE gbFontAllowWrap;
#endif
#endif

// 0x508454 off_508454, 0x50846C dword_50846C and 0x508484 dword_508484 -- six
// entries each, one per race and outcome: the bitmap directory, the glues mode
// to return to and the music track.
#ifndef OC_OFF_508454
#define OC_OFF_508454
#ifdef OC_DLL_MODE
#define off_508454 (reinterpret_cast<const char **>(OC_VA(0x508454u)))
#define dword_50846C (reinterpret_cast<DWORD *>(OC_VA(0x50846Cu)))
#define dword_508484 (reinterpret_cast<int *>(OC_VA(0x508484u)))
#else
extern const char *off_508454[6];
extern DWORD dword_50846C[6] OC_UNALIGNED;
extern int dword_508484[6] OC_UNALIGNED;
#endif
#endif

// 0x66B41C sgpScoreDlg_g -- the loaded score dialog.
#ifndef OC_SGPSCOREDLG_G
#define OC_SGPSCOREDLG_G
#ifdef OC_DLL_MODE
#define sgpScoreDlg_g (*reinterpret_cast<ctrl_t **>(OC_VA(0x66B41Cu)))
#else
extern ctrl_t *sgpScoreDlg_g;
#endif
#endif

// 0x66B418 gdwGameId_g -- the league game id: sub_461B80 writes it and
// game.cpp reads it back.
#ifndef OC_GDWGAMEID_G
#define OC_GDWGAMEID_G
#ifdef OC_DLL_MODE
#define gdwGameId_g (*reinterpret_cast<DWORD *>(OC_VA(0x66B418u)))
#else
extern DWORD gdwGameId_g;
#endif
#endif

// 0x52701C and 0x527120, the map path and the map name, spelled and sized as
// game.cpp spells them.
#ifndef OC_BYTE_52701C_G
#define OC_BYTE_52701C_G
#ifdef OC_DLL_MODE
#define byte_52701C_g (reinterpret_cast<char *>(OC_VA(0x52701Cu)))
#else
extern char byte_52701C_g[0x104] OC_UNALIGNED;
#endif
#endif

#ifndef OC_BYTE_527120
#define OC_BYTE_527120
#ifdef OC_DLL_MODE
#define byte_527120 (reinterpret_cast<char *>(OC_VA(0x527120u)))
#else
extern char byte_527120[0x20];
#endif
#endif

// net_misc.cpp 0x477270, spelled as VictDlg.cpp spells it: fills the eight
// per-storm-id results and the local player's.
void __fastcall sub_477270(int *pnStatus, int *pnLocal);

// The bodies, in oracle VA order.

// Print a duration as "N hours, M minutes", dropping the hours when there are
// none. The unit words come from the error-string table and each has a
// singular form one id above the plural.
void sub_4618A0(char *pszBuf, unsigned int nSeconds, unsigned int cchBuf)
{
  char *psz = pszBuf;
  unsigned int nUnits;
  const char *pszUnit;

  if (nSeconds >= 3600) {
    nUnits = nSeconds / 3600;
    if (nUnits != 1)
      pszUnit = error_get_str(12);
    else
      pszUnit = error_get_str(13);
    psnprintf(psz, cchBuf, "%u %s, ", nUnits, pszUnit);
    psz += strlen(psz);
    nSeconds -= nUnits * 3600;
  }
  nUnits = nSeconds / 60;
  if (nUnits != 1)
    pszUnit = error_get_str(14);
  else
    pszUnit = error_get_str(15);
  psnprintf(psz, cchBuf, "%u %s", nUnits, pszUnit);
}

// Run the end-of-game score screen: record the outcome, pick the race and
// outcome's art, music and return mode, load the art, run the dialog, then
// settle where the glues go next.
void do_score_dlg(int nArg0)
{
  int nIdx;
  char szPath[0x104];

  sub_477270(dword_66CA70, &dword_66CA90);
  dword_66C950 = dword_66CA90 == 1;
  if (gbWasDemoMode_g)
    nIdx = 1;
  else
    nIdx = gPlayerData[g_LocalNationID].nRace * 2 + (dword_66C950 != 0);
  glGluesMode = dword_50846C[nIdx];
  strcpy(byte_66C958, off_508454[nIdx]);
  snd_play_music(dword_508484[nIdx]);
  strcpy(szPath, byte_66C958);
  strcat(szPath, "iScore.grp");
  dword_66C954 = ReadGroupFile(szPath, 0, "gluScore.cpp", 0x560);
  strcpy(szPath, byte_66C958);
  strcat(szPath, "tminimap.pcx");
  GameLoadImage(reinterpret_cast<intptr_t>(szPath), 0, reinterpret_cast<intptr_t>(byte_66CA5C), 0xC, 0, 0, 0);
  strcpy(byte_66C958, off_508454[nIdx]);
  sub_49E350(0xA8);
  // Hyperion: Hyperion's score screen in the dialog's place, every player of the game
  // listed, with the outcome worked out above; the dialog when the front end is off
  ScoreEnd end;
  end.nLocal = dword_66CA90;
  end.bReplay = gbWasDemoMode_g != 0;
  end.bSaveable = !(gbWasDemoMode_g != 0 || gbWasLoaded_g != 0 || word_526524 != 0 || IsSpawnedCopy());
  end.nSeconds = gbWasDemoMode_g != 0 ? replay_dlg_elapsed_seconds() : sub_47A780();
  int nResult = PortFrontScore(end);
  if (nResult < 0) {
    sgpScoreDlg_g = dlg_load("rez\\gluScore.bin",
                             reinterpret_cast<PFN_DLGLOADER>(load_game_data),
                             reinterpret_cast<PFN_DLGMEASURE>(alloc_load_bmp),
                             "gluScore.cpp", 0x56F);
    nResult = dlg_run_modal(sgpScoreDlg_g, 0, gluscore_dlg_fcn);
  }
  if (nResult != 7) {
    sub_4BB970();
    glGluesMode = 0;
  } else if (gbMultiPlayerMode) {
    set_netmode_return_menu();
    // Hyperion: a LAN room's match: the network left and home next, where the room is,
    // not the provider's list of games (port_lanlobby.cpp)
    if (PortLanBackToFront()) {
      NetworkShutdown();
      gbMultiPlayerMode = 0;
      glGluesMode = 0;
    }
  } else {
    glGluesMode = sub_4BB790(dword_66C950) ? glGluesMode : 0;
  }
  glues_clear_all();
  // Hyperion: OK on Hyperion's score screen goes back to the skirmish or replays screen,
  // which have the menus' music: the score's fades into it as that screen comes (the user,
  // 2026-10-26). Elsewhere it stops, as 1.17's does.
  if (PortFrontScoreToScreen())
    PortMusicChange(0x16);
  else
    stop_music();
  mem_free(dword_66C954, "gluScore.cpp", 0x581);
  dword_66CA94 = 0;
}

// Record which slot a shuffled player came from. The caller is one arm of a
// swap loop and hands over a slot index and a zero-extended byte.
void sub_461B70(int nSlot, int nOrigSlot)
{
  gbSlotRemap_g[nSlot] = (BYTE)nOrigSlot;
}

// One line of the league report: the tab fill callback, its format and three
// ids the report never reads.
struct scorereport_t {
  /*0x00*/ PFN_SCOREFILL pfnFill;
  /*0x04*/ const char *pszFormat;
  /*0x08*/ int anId[3];
};
OC_X86_LAYOUT(sizeof(scorereport_t) == 0x14, "lea esi, [esi+14h] per line");
static_assert(offsetof(scorereport_t, pfnFill) == 0x00, "push dword ptr [esi-4h]");
OC_X86_LAYOUT(offsetof(scorereport_t, pszFormat) == 0x04, "push dword ptr [esi]");
OC_X86_LAYOUT(offsetof(scorereport_t, anId) == 0x08, "mov dword ptr [ebp-6Ch], 4h");

// Build the league result report: the game id is the xor of the SHA-1 words of
// the player name, map path and local time; the detail block carries the
// league, game id, race and time, then the local player's four score lines.
// The indexed loop that re-reads the row at each use is what puts the line
// pointer at +4 and spills the counter into cchDetail's home.
int __fastcall sub_461B80(char *pszHeader, int cchHeader, char *pszDetail, int cchDetail)
{
  if (pszHeader == NULL || pszDetail == NULL || dword_66CA94 == 0)
    return 0;

  int nRaceStr = 8;
  switch (byte_5264C2) {
    case 0:
      nRaceStr = 7;
      break;
    case 1:
      nRaceStr = 8;
      break;
    case 2:
      nRaceStr = 9;
      break;
  }

  char szSeed[0x400];
  __time64_t tNow = _time64(NULL);
  SStrPrintf(szSeed, 0x400, "%s%s%s", gLocalPlayerName, byte_52701C_g, _ctime64(&tNow));

  shactx_t ctx;
  DWORD adwHash[5];
  SHA1_Init(&ctx);
  SHA1_Update(&ctx, szSeed, (DWORD)strlen(szSeed));
  SHA1_Final(adwHash, &ctx);
  gdwGameId_g = adwHash[0] ^ adwHash[1] ^ adwHash[2] ^ adwHash[3] ^ adwHash[4];

  int nLeague = 0;
  SNetGetLeagueName(&nLeague);

  char szTime[0xFF] = {0};
  sub_4618A0(szTime, sub_47A780(), 0xFF);

  psnprintf(pszDetail, cchDetail,
            "<leagueid>%d</leagueid>\n<gameid>0x%08x</gameid>\n<race>%s</race>\n<time>%u</time>\n",
            nLeague, gdwGameId_g, error_get_str(nRaceStr), sub_47A780());

  scorereport_t aLines[4] = {
    {sub_462B60, "<score overall=\"%u\" units=\"%u\" structures=\"%u\" resources=\"%u\"/>\n", {4, 5, 6}},
    {sub_462C10, "<units score=\"%u\" produced=\"%u\" killed=\"%u\" lost=\"%u\"/>\n", {7, 8, 9}},
    {sub_462B20, "<structures score=\"%u\" constructed=\"%u\" razed=\"%u\" lost=\"%u\"/>\n", {10, 11, 12}},
    {sub_462BB0, "<resources score=\"%u\" gas=\"%u\" minerals=\"%u\" spent=\"%u\"/>\n", {13, 14, 15}},
  };

  for (int i = 0; i < 4; i++) {
    char szLine[0x400];
    sub_4629F0(sgScoreRows_g, sgScoreSlots_g, 8, aLines[i].pfnFill);
    psnprintf(szLine, 0x400, aLines[i].pszFormat, sgScoreRows_g[g_LocalNationID].lTotal,
              sgScoreRows_g[g_LocalNationID].lValue[0], sgScoreRows_g[g_LocalNationID].lValue[1],
              sgScoreRows_g[g_LocalNationID].lValue[2]);
    SStrPack(pszDetail, "\n  ", cchDetail);
    SStrPack(pszDetail, szLine, cchDetail);
  }

  psnprintf(pszHeader, cchHeader, "<map>%s</map>\n", byte_527120);
  return 1;
}

// Reset the score rows and fill the eight slot records from gPlayerData: the
// name (or "Computer" for a computer in a multiplayer game), race, team or force
// caption and color slot, counting the live slots into dword_66CA6C. An
// observer slot at the local player sets dword_66CA68.
void sub_461E00()
{
  memset(sgScoreRows_g, 0, 8 * sizeof(sgScoreRows_g[0]));  // Hyperion: 0x12E0 in 1.17
  memset(sgScoreSlots_g, 0, 0x240);
  dword_66CA68 = 0;
  dword_66CA6C = 0;
  for (int i = 0; i < 8; i++) {
    sgScoreSlots_g[i].nType = 0;
    switch (gPlayerData[i].nType) {
    case 1:
    case 11:
      if (!gbMultiPlayerMode && !gPlayerData[i].szName[0])
        continue;
    case 2:
    case 10:
      if (gbMultiPlayerMode)
        SStrCopy(sgScoreSlots_g[i].szName,
                 gPlayerData[i].nType == 2 || gPlayerData[i].nType == 10
                     ? gPlayerData[i].szName
                     : error_get_str(0x0B),
                 0x19);
      else
        SStrCopy(sgScoreSlots_g[i].szName, gPlayerData[i].szName, 0x19);
      sgScoreSlots_g[i].bRace = gPlayerData[i].nRace;
      if (IsTeamGame()) {
        const char *pszTeam = GetTeamName(gPlayerData[i].nTeam);
        SStrCopy(sgScoreSlots_g[i].szLabel, pszTeam, 0x1E);
        sgScoreSlots_g[i].nPlayer = team_get_slot_captain(gPlayerData[i].nTeam);
      } else {
        sgScoreSlots_g[i].nPlayer = i;
      }
      if (IsScenarioGame(NULL))
        GetForceName(gPlayerData[i].nTeam, sgScoreSlots_g[i].szLabel, 0x1E);
      dword_66CA6C++;
      break;
    case 9:
      if (i == g_LocalNationID)
        dword_66CA68 = 1;
      break;
    }
    sgScoreSlots_g[i].nType = gPlayerData[i].nType;
    sgScoreSlots_g[i].nStormId = gPlayerData[i].dwStormID;
  }
  dword_66CA94 = 1;
}

// Release the score dialog's two owned buffers. Control 1 keeps its allocation
// in the signed +0x10 slot rather than in pData, and only that one is guarded;
// control 9's pData is freed unconditionally.
void gluscore_dlg_deactivate(ctrl_t *pDlg)
{
  ctrl_t *pCtrl = ctrl_find_ndx(pDlg, 1);

  if (pCtrl->nResIndex != 0) {
    SMemFree(reinterpret_cast<void *>(pCtrl->nResIndex), "gluScore.cpp", 0x43D, 0);
    pCtrl->nResIndex = 0;
  }
  pCtrl = ctrl_find_ndx(pDlg, 9);
  SMemFree(pCtrl->pData, "gluScore.cpp", 0x3D7, 0);
  gbWasDemoMode_g = 0;
}

// Remember that a game was played, so the score screen knows it has data.
void gluscore_set_loaded(int bLoaded)
{
  gbWasLoaded_g = bLoaded;
}

// Bind the live rows to their controls, six per row starting at 0x0D: the
// portrait, the name, the three bars and the total, each given the row and its
// draw handler and shown. The local player's name is flagged. The controls of
// the rows past the live count are hidden.
void sub_4626E0(ctrl_t *pDlg)
{
  int nRow = dword_66CA6C;
  for (int i = 0; i < nRow; i++, nRow = dword_66CA6C) {
    int nIndex = static_cast<WORD>(i);
    int nBase = nIndex * 6;
    ctrl_t *pCtl = ctrl_find_ndx(pDlg, nBase + 0x0D);
    ctrl_row(pCtl) = &sgScoreRows_g[i];
    ctrl_show(pCtl);
    pCtl = ctrl_find_ndx(pDlg, nBase + 0x0E);
    ctrl_row(pCtl) = &sgScoreRows_g[i];
    pCtl->pfnDraw = sub_462C50;
    ctrl_set_text(pCtl, sgScoreSlots_g[sgScoreRows_g[i].nSlot].szName);
    ctrl_show(pCtl);
    sgScoreRows_g[i].pCtrlName = pCtl;
    if (sgScoreRows_g[i].nSlot == g_LocalNationID)
      pCtl->dwFlags |= 0x40000000;
    else
      pCtl->dwFlags &= ~0x40000000;
    for (int k = 0; k < 3; k++) {
      int nCol = static_cast<WORD>(k);
      pCtl = ctrl_find_ndx(pDlg, nBase + 0x0F + nCol);
      *reinterpret_cast<WORD *>(pCtl->_unmapped24) = static_cast<WORD>(nCol);
      pCtl->pfnDraw = sub_4628C0;
      ctrl_row(pCtl) = &sgScoreRows_g[i];
      ctrl_set_text(pCtl, sgScoreRows_g[i].szBar[k]);
      ctrl_show(pCtl);
      sgScoreRows_g[i].pCtrlBar[k] = pCtl;
    }
    pCtl = ctrl_find_ndx(pDlg, (nIndex + 3) * 6);
    ctrl_row(pCtl) = &sgScoreRows_g[i];
    pCtl->pfnDraw = sub_462E00;
    ctrl_set_text(pCtl, sgScoreRows_g[i].szTotal);
    ctrl_show(pCtl);
    sgScoreRows_g[i].pCtrlTotal = pCtl;
  }
  for (; nRow < 8; nRow++) {
    int nIndex = static_cast<WORD>(nRow);
    int nBase = nIndex * 6;
    ctrl_hide(ctrl_find_ndx(pDlg, nBase + 0x0D));
    ctrl_hide(ctrl_find_ndx(pDlg, nBase + 0x0E));
    for (int k = 0; k < 3; k++)
      ctrl_hide(ctrl_find_ndx(pDlg, nBase + 0x0F + k));
    ctrl_hide(ctrl_find_ndx(pDlg, (nIndex + 3) * 6));
  }
}

// A bar control's draw callback: the column's full track, then the player's
// colored bar scaled against the column maximum (at least one pixel for a
// nonzero value), then the caption. Both frames come from the player's color.
void sub_4628C0(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scorerow_t *pRow = ctrl_row(pCtrl);
  if (!pRow->bUsed)
    return;
  void *pSavedGrp = gpCurrDlgGrp_g;
  int nCol = *reinterpret_cast<WORD *>(pCtrl->_unmapped24);
  gpCurrDlgGrp_g = dword_66C954;
  LONG nValue = pRow->lAnim[nCol] / pRow->lScale[nCol];
  unsigned nWidth = pCtrl->rc.right - pCtrl->rc.left + 1;
  unsigned nMax = qword_66B424[nCol];
  int nColor = get_slot_mapping(sgScoreSlots_g[pRow->nSlot].nPlayer);
  if (gbSlotRemap_g[nColor])
    nColor = gbSlotRemap_g[nColor];
  int nBarFrame = nColor * 6 + 6;
  int nTrackFrame = nColor * 6 + 9;
  unsigned nLen;
  if (nMax) {
    nLen = nWidth * nValue / nMax;
    if (nLen > nWidth) {
      nLen = nWidth;
      goto draw;
    }
  } else {
    nLen = 0;
  }
  if (nValue && !nLen)
    nLen = 1;
draw:
  if (nWidth)
    draw_local_ctrl(pCtrl->rc.left, pCtrl->rc.top, nWidth, gwUnk29_g,
                    static_cast<unsigned char>(nTrackFrame), &pCtrl->rc, 0x2000);
  if (nLen)
    draw_local_ctrl(pCtrl->rc.left, pCtrl->rc.top, nLen, gwUnk29_g,
                    static_cast<unsigned char>(nBarFrame), &pCtrl->rc, 0x2000);
  gpCurrDlgGrp_g = pSavedGrp;
  if (pCtrl->pData != NULL) {
    gbFontJust = 0x12;
    draw_ctrl_text(pCtrl, 0, 2, NULL);
  }
}

// Fill the used rows from the live slots through the tab's callback, then give
// the total and each bar an animation scale (1, 8 or 16 by how small the value
// is against 256) and a scaled target. Read the fields each time rather than
// through locals: the use counts are what bias the row pointer to lTotal and the
// slot pointer to nType. pSlots steps before pRows.
void sub_4629F0(scorerow_t *pRows, scoreslot_t *pSlots, int nCount,
                PFN_SCOREFILL pfnFill)
{
  for (int i = 0; i < nCount; i++, pSlots++, pRows++) {
    if (pSlots->nType != 2 && pSlots->nType != 10 && pSlots->nType != 1 && pSlots->nType != 11)
      continue;
    pRows->bUsed = 1;
    pRows->nSlot = i;
    pfnFill(pRows, pSlots->nPlayer);
    LONG s;
    if (pRows->lTotal == 0 || (s = 0x100 / pRows->lTotal) == 0)
      s = 1;
    else if (s <= 8) {
      if (s > 1)
        s = 8;
    } else
      s = s > 1 ? 16 : s;
    pRows->lTotalScale = s;
    pRows->lTotalTarget = pRows->lTotal * s;
    for (int k = 0; k < 3; k++) {
      if (pRows->lValue[k] == 0 || (s = 0x100 / pRows->lValue[k]) == 0)
        s = 1;
      else if (s <= 8) {
        if (s > 1)
          s = 8;
      } else
        s = s > 1 ? 16 : s;
      pRows->lScale[k] = s;
      pRows->lTarget[k] = pRows->lValue[k] * s;
    }
  }
}

// Fill a row from the units tab: units built, lost and killed, with the total
// taken from the two buildings-lost rows rather than from the three above it.
void sub_462B20(scorerow_t *pRow, int nPlayer)
{
  pRow->lValue[0] = glGameCounts_g[9][nPlayer];
  pRow->lValue[1] = glGameCounts_g[11][nPlayer];
  pRow->lValue[2] = glGameCounts_g[10][nPlayer];
  pRow->lTotal = glGameCounts_g[12][nPlayer] + glGameCounts_g[13][nPlayer];
}

// Fill the custom-score row: two pairs of game counts and the resources
// gathered, with the total the sum of all three.
void sub_462B60(scorerow_t *pRow, int nPlayer)
{
  LONG lAcc = glGameCounts_g[5][nPlayer] + glGameCounts_g[6][nPlayer];
  LONG lPart;

  pRow->lValue[0] = lAcc;
  lPart = glGameCounts_g[12][nPlayer] + glGameCounts_g[13][nPlayer];
  lAcc += lPart;
  pRow->lValue[1] = lPart;
  lPart = gPlayerGasGathered_g[nPlayer] + gPlayerMineralsGathered_g[nPlayer];
  pRow->lValue[2] = lPart;
  pRow->lTotal = lPart + lAcc;
}

// Fill the resources row: gas and minerals gathered, then what was spent,
// which is the difference against what is still on hand and never negative.
void sub_462BB0(scorerow_t *pRow, int nPlayer)
{
  LONG lGathered;
  LONG lSpent;

  pRow->lValue[0] = gPlayerGasGathered_g[nPlayer];
  pRow->lValue[1] = gPlayerMineralsGathered_g[nPlayer];
  lGathered = gPlayerMineralsGathered_g[nPlayer] + gPlayerGasGathered_g[nPlayer];
  lSpent = lGathered - gPlayerMinerals[nPlayer] - gPlayerGas[nPlayer];
  if (lSpent < 0)
    lSpent = 0;
  pRow->lTotal = lGathered;
  pRow->lValue[2] = lSpent;
}

// Fill the buildings row, the same shape as sub_462B20 over the lower rows.
void sub_462C10(scorerow_t *pRow, int nPlayer)
{
  pRow->lValue[0] = glGameCounts_g[2][nPlayer];
  pRow->lValue[1] = glGameCounts_g[4][nPlayer];
  pRow->lValue[2] = glGameCounts_g[3][nPlayer];
  pRow->lTotal = glGameCounts_g[5][nPlayer] + glGameCounts_g[6][nPlayer];
}

// A name control's draw callback: the race icon, a box in the player's color
// sized to the icon, the caption clipped to 0xB8, then the slot's second
// caption in font 0 beside the box.
void sub_462C50(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  scorerow_t *pRow = ctrl_row(pCtrl);
  BYTE bFrame = sgScoreSlots_g[pRow->nSlot].bRace + 3;
  bmp_t bmp;
  rect16_t rc;
  short nWidth, nHeight;

  rc = pCtrl->rc;
  rc.left += 8;
  rc.top += 0x14;
  {
    short x, y, w, h;
    grp_to_bmpx(static_cast<GROUP *>(dword_66C954), bFrame, &bmp, &x, &y, &w, &h);
  }
  draw_bmp_effect(&bmp, 0, 0, &rc, 0x2000);

  bmp_t bmpBox;
  {
    short x, y;
    grp_to_bmpx(static_cast<GROUP *>(dword_66C954), bFrame, &bmpBox, &x, &y, &nWidth, &nHeight);
  }
  nWidth = bmpBox.nWidth;
  nHeight = bmpBox.nHeight;
  int nColor = get_slot_mapping(sgScoreSlots_g[pRow->nSlot].nPlayer);
  if (gbSlotRemap_g[nColor])
    nColor = gbSlotRemap_g[nColor];
  _bmp_pen = byte_66CA5C[nColor];
  bmp_rect(pCtrl->rc.left + 8, pCtrl->rc.top + 0x14, nWidth, nHeight);

  if (pCtrl->pData != NULL) {
    BYTE bWrap = gbFontAllowWrap;
    gbFontJust = 0x11;
    gbFontAllowWrap = 0;
    rc = pCtrl->rc;
    rc.right = 0xB8;
    draw_ctrl_text(pCtrl, 0, 0, &rc);
    gbFontAllowWrap = bWrap;
  }

  char *pszLabel = sgScoreSlots_g[pRow->nSlot].szLabel;
  if (*pszLabel) {
    get_printable_string(pszLabel, pszLabel, 0x1E);
    BYTE bWrap = gbFontAllowWrap;
    gbFontAllowWrap = 0;
    font_set(sgpFonts_g[0]);
    font_set_color_tbl(6);
    gsFontMargins = pCtrl->rc;
    gbFontJust = 0x11;
    font_adjust(pCtrl->rc.left + (WORD)nWidth + 0x28, pCtrl->rc.top + 0x14, pszLabel);
    font_set(NULL);
    gbFontAllowWrap = bWrap;
  }
}

// Draw the total control: its own bitmap over its own rect, then the caption
// if it has one. The clip rectangle and both offsets are ignored.
void sub_462E00(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  draw_bmp_effect(reinterpret_cast<bmp_t *>(&pCtrl->_unmapped0C[0]), 0, 0,
                  &pCtrl->rc, 0x2000);
  if (pCtrl->pData != NULL) {
    gbFontJust = 0x12;
    draw_ctrl_text(pCtrl, 4, 2, NULL);
  }
}

// The save-replay button. It grays itself on activation when there is nothing
// to save, and on its own click it offers the dialog and grays itself once the
// replay is written.
int gluScore_flc_saveReplay_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 0:
      if (gbWasDemoMode_g != 0 || gbWasLoaded_g != 0 || word_526524 != 0 ||
          IsSpawnedCopy())
        ctrl_gray(pCtrl);
      break;
    case 2:
      if (do_replay_dlg(gPlayerData[g_LocalNationID].nRace, dword_66C950)) {
        ctrl_set_text(pCtrl, glues_get_str(0xB1));
        ctrl_gray(pCtrl);
      }
      return 1;
    }
  }
  return ctrl_btnglow_flc(pCtrl, pEvent);
}

// The score screen's dialog proc. Arm order follows the jump table at
// 0x462390 over the selector bytes at 0x4623A4, and the three arms that fall
// out share the tail call to ctrl_dlg.
int gluscore_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 10:
      glues_transition_in(reinterpret_cast<intptr_t>(pCtrl), sScoreAnims_g,
                          1, 0, 0x1F4);
      glue_init_screen_dlg(pCtrl, sScoreEvtFcnTbl_g, 0xF4, 0);
      break;
    case 0:
      gluscore_dlg_activate(pCtrl);
      glue_screen_activate(pCtrl);
      break;
    case 2:
      stop_digital();
      return glues_transition_out(pCtrl);
    case 1:
      Sleep(1000);
      gluscore_dlg_deactivate(pCtrl);
      break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// Draw a tab: its own bitmap 0x1C pixels down from its rect, then the caption.
// The hot bit is cleared for the draw and restored afterwards, and a tab whose
// flags are negative draws no bitmap at all.
void gluScore_tab_ctrl_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip)
{
  LONG lSaved = pCtrl->dwFlags;
  LONG lFlags = lSaved & ~0x80;

  pCtrl->dwFlags = lFlags;
  if ((lFlags & 0x80000000) != 0) {
    lFlags |= 0x80;
    pCtrl->dwFlags = lFlags;
  } else {
    rect16_t rc = pCtrl->rc;

    rc.top = rc.top + 0x1C;
    draw_bmp_effect(reinterpret_cast<bmp_t *>(&pCtrl->_unmapped0C[0]), 0, 0,
                    &rc, 0x2000);
  }
  if (pCtrl->pData != NULL) {
    gbFontJust = 0x22;
    draw_ctrl_text(pCtrl, 0, 0, NULL);
  }
  pCtrl->dwFlags = lSaved;
}

// Whether the slot's player finished with nResult, 1 for a win and 2 for a
// loss. Observers (types 1 and 11) have no result.
static inline BOOL score_slot_result(int nSlot, LONG nResult)
{
  if (sgScoreSlots_g[nSlot].nType == 1 || sgScoreSlots_g[nSlot].nType == 11)
    return FALSE;
  int nStormId = sgScoreSlots_g[nSlot].nStormId;
  return dword_66CA70[nStormId] == nResult ||
         (g_LocalStormID == nStormId && dword_66CA90 == nResult);
}

// The rank sub_4623C0 returns. Forced inline because sub_4630C0 inlines it
// for the row it is placing while it calls sub_4623C0 for the rows it scans.
static __forceinline int score_row_rank(scorerow_t *pRow, int nIndex)
{
  int nRank;

  if (pRow->bUsed == 0)
    return -0x7FFFFFFF;
  nRank = pRow->lTotal * 8 + 8 - nIndex;
  if (score_slot_result(pRow->nSlot, 1))
    nRank += 0x3FFFFFFF;
  if (score_slot_result(pRow->nSlot, 2))
    nRank -= 0x3FFFFFFF;
  return nRank;
}

// Rank one row for the sort: eight times the total, biased down by the row's
// index, then pushed to an extreme when the slot has won or lost. The win and
// loss tests are one inlined predicate, so the slot index is re-scaled in each.
int sub_4623C0(scorerow_t *pRow, int nIndex)
{
  return score_row_rank(pRow, nIndex);
}

// Make pCtrl the selected tab: deselect the four tabs, counting those at or
// before it into dword_66B420, then on a change reset the rows, retitle the
// three columns, refill through that tab's callback and restart the 0x22 timer.
void gluScore_tab_ctrl_updatetab(ctrl_t *pCtrl)
{
  dword_66B420 = 0;
  ctrl_t *pWasSelected = NULL;
  ctrl_t *pTab = ctrl_find_ndx(pCtrl, 3);
  for (int n = 4; n != 0; n--) {
    if (*reinterpret_cast<LONG *>(pTab->_unmapped36) != 0)
      pWasSelected = pTab;
    *reinterpret_cast<LONG *>(pTab->_unmapped36) = 0;
    pTab->dwFlags = (pTab->dwFlags & 0x5FFFFFFF) | 0x20000000;
    ctrl_set_update(pTab);
    pTab = ctrl_find_ndx(pCtrl, pTab->nIndex + 1);
    if (pTab->nIndex <= pCtrl->nIndex)
      dword_66B420++;
  }
  pCtrl->dwFlags |= 0x80000000;
  *reinterpret_cast<LONG *>(pCtrl->_unmapped36) = 1;
  if (pCtrl == pWasSelected)
    return;
  ctrl_t *pDlg = pCtrl->pParent;
  memset(qword_66B424, 0, 3 * sizeof(LONG));
  memset(sgScoreRows_g, 0, 8 * sizeof(sgScoreRows_g[0]));  // Hyperion: 0x12E0 in 1.17
  stop_digital();
  timer_remove_node(pDlg, 0x22);
  for (int i = 0; i < 3; i++)
    glue_set_text(pDlg, i + 10, glues_get_str(sScoreStrs_g[dword_66B420][i]));
  switch (dword_66B420) {
  case 0:
    sub_4629F0(sgScoreRows_g, sgScoreSlots_g, 8, sub_462B60);
    break;
  case 1:
    sub_4629F0(sgScoreRows_g, sgScoreSlots_g, 8, sub_462C10);
    break;
  case 2:
    sub_4629F0(sgScoreRows_g, sgScoreSlots_g, 8, sub_462B20);
    break;
  case 3:
    sub_4629F0(sgScoreRows_g, sgScoreSlots_g, 8, sub_462BB0);
    break;
  }
  sub_4630C0(sgScoreRows_g, 8);
  sub_4626E0(pDlg);
  timer_create(pDlg, 0x22, 0x14, reinterpret_cast<PFN_TIMER_FN *>(sub_462F70));
  if (qword_66B424[0] + qword_66B424[1] + qword_66B424[2] > 0)
    sub_49E740(0xA8);
}

// Set the dialog's heading from the outcome, mark it and control 8 as
// owner-drawn, then load the screen's main bitmap from the directory the tab
// selection left in byte_66C958.
void gluscore_dlg_activate(ctrl_t *pDlg)
{
  char szPath[0x104];
  ctrl_t *pCtrl;
  char *pszText;

  pCtrl = ctrl_find_ndx(pDlg, 2);
  pszText = NULL;
  switch (dword_66CA90) {
  case 1:
    pszText = glues_get_str(18);
    break;
  case 0:
  case 3:
    pszText = glues_get_str(19);
    break;
  case 4:
    pszText = glues_get_str(20);
    break;
  case 2:
    pszText = glues_get_str(17);
    break;
  }
  if (gbWasDemoMode_g != 0) {
    ctrl_set_text(pCtrl, glues_get_str(0xB2));
    pCtrl->dwFlags |= 0x40000000;
  } else if (pszText != NULL) {
    ctrl_set_text(pCtrl, pszText);
    pCtrl->dwFlags |= 0x40000000;
  }
  ctrl_find_ndx(pDlg, 8)->dwFlags |= 0x40000000;
  strcpy(szPath, byte_66C958);
  strcat(szPath, "pMain.pcx");
  pCtrl = ctrl_find_ndx(pDlg, 1);
  alloc_load_bmp(szPath, reinterpret_cast<bmp_t *>(&pCtrl->_unmapped0C[0]), 0,
                 "gluScore.cpp", 0x432);
  ctrl_set_update(pCtrl);
  sub_4625F0(pDlg);
}

// Build the elapsed-time caption for control 9 and hand it a freshly allocated
// string. The clock comes from the replay when the score screen was reached
// from a loaded game. `nSeconds / 60 % 60` as one expression, after the hours,
// is what emits the oracle's `div ebx`; a named minutes local does not.
void sub_4625F0(ctrl_t *pDlg)
{
  char szTime[0x80];
  ctrl_t *pCtrl;
  unsigned int nSeconds;
  unsigned int nHours;
  unsigned int nMins;
  unsigned int nSecs;
  char *pszFmt;
  DWORD cchText;
  char *pszText;

  pCtrl = ctrl_find_ndx(pDlg, 9);
  nSeconds = sub_47A780();
  if (gbWasDemoMode_g != 0)
    nSeconds = replay_dlg_elapsed_seconds();
  nHours = nSeconds / 3600;
  nMins = nSeconds / 60 % 60;
  nSecs = nSeconds % 60;
  if (nHours != 0)
    psnprintf(szTime, 0x80, "%d:%02d:%02d", nHours, nMins, nSecs);
  else
    psnprintf(szTime, 0x80, "%02d:%02d", nMins, nSecs);
  pszFmt = glues_get_str(16);
  cchText = SStrLen(pszFmt) + SStrLen(szTime);
  pszText = static_cast<char *>(SMemAlloc(cchText, "gluScore.cpp", 0x3CD, 0));
  psnprintf(pszText, cchText, pszFmt, szTime);
  pCtrl->pData = pszText;
}

// The per-player scorebox frame. It loads its bitmap once on activation and
// releases it on the dialog's teardown event, and otherwise behaves as a
// plain text control.
int gluScore_player_scorebox_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  char szPath[0x104];

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 10:
      if (dword_66CA98 == 0) {
        strcpy(szPath, byte_66C958);
        strcat(szPath, "scorebox.pcx");
        alloc_load_bmp(szPath, &sgGluScoreboxBmp_g, 0, "gluScore.cpp", 0x1A5);
        dword_66CA98 = 1;
      }
      *ctrl_bmp(pCtrl) = sgGluScoreboxBmp_g;
      pCtrl->dwFlags |= 0x20;
      ctrl_set_update(pCtrl);
      break;
    case 1:
      if (dword_66CA98 != 0) {
        if (sgGluScoreboxBmp_g.pPixels != NULL) {
          SMemFree(sgGluScoreboxBmp_g.pPixels, "gluScore.cpp", 0x1B0, 0);
          sgGluScoreboxBmp_g.pPixels = NULL;
          ctrl_bmp(pCtrl)->pPixels = NULL;
        }
      }
      dword_66CA98 = 0;
      break;
    }
  }
  return ctrl_text(pCtrl, pEvent);
}

// The player inset frame, the scorebox's twin over its own bitmap. Its extra
// arm answers event 13 by reporting whether the row behind the control is in
// use.
int gluScore_player_inset_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  char szPath[0x104];

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 10:
      if (dword_66CAA4 == 0) {
        strcpy(szPath, byte_66C958);
        strcat(szPath, "pInset.pcx");
        alloc_load_bmp(szPath, &sgGluScoreInsetBmp_g, 0, "gluScore.cpp", 0x1C7);
        dword_66CAA4 = 1;
      }
      *ctrl_bmp(pCtrl) = sgGluScoreInsetBmp_g;
      pCtrl->dwFlags |= 0x20;
      ctrl_set_update(pCtrl);
      break;
    case 13:
      if (ctrl_row(pCtrl) == NULL || ctrl_row(pCtrl)->bUsed == 0)
        return 1;
      break;
    case 1:
      if (dword_66CAA4 != 0) {
        if (sgGluScoreInsetBmp_g.pPixels != NULL) {
          SMemFree(sgGluScoreInsetBmp_g.pPixels, "gluScore.cpp", 0x1D9, 0);
          sgGluScoreInsetBmp_g.pPixels = NULL;
          ctrl_bmp(pCtrl)->pPixels = NULL;
        }
      }
      dword_66CAA4 = 0;
      break;
    }
  }
  return ctrl_text(pCtrl, pEvent);
}

// The tab buttons. Each loads the shared inactive-tab bitmap once, installs
// the tab draw handler, and drives the tab switch on a click, on a page change
// and on the dialog's own refresh.
int gluScore_tab_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent)
{
  char szPath[0x104];

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
    case 10:
      if (dword_66CAB0 == 0) {
        strcpy(szPath, byte_66C958);
        strcat(szPath, "untab.pcx");
        alloc_load_bmp(szPath, &szGluScoreTabBmp_g, 0, "gluScore.cpp", 0x224);
        dword_66CAB0 = 1;
      }
      pCtrl->pfnDraw = gluScore_tab_ctrl_draw;
      // Hyperion: the whole bmp_t. 1.17 copied it as two dwords, the flag set
      // between the second half's load and its store, which at 64 bits left the
      // tab without its pixel pointer, and the score screen crashed drawing it
      // (../docs/upstream-notes.md, 2026-09-30).
      *ctrl_bmp(pCtrl) = szGluScoreTabBmp_g;
      pCtrl->dwFlags |= 0x20;
      ctrl_set_update(pCtrl);
      break;
    case 13:
      if (pCtrl == ctrl_find_ndx(pCtrl, 3))
        gluScore_tab_ctrl_updatetab(pCtrl);
      break;
    case 11:
      if (pEvent->dwParam != 0)
        gluScore_tab_ctrl_updatetab(pCtrl);
      return 1;
    case 2:
      gluScore_tab_ctrl_updatetab(pCtrl);
      return 1;
    case 1:
      if (dword_66CAB0 != 0) {
        if (szGluScoreTabBmp_g.pPixels != NULL) {
          SMemFree(szGluScoreTabBmp_g.pPixels, "gluScore.cpp", 0x23E, 0);
          szGluScoreTabBmp_g.pPixels = NULL;
        }
      }
      ctrl_bmp(pCtrl)->pPixels = NULL;
      dword_66CAB0 = 0;
      break;
    }
  }
  return btn_ctrl_radiobutton(pCtrl, pEvent);
}

// Step every counter an eighth of the way to its target, at least four units
// at a time, and repaint its caption. When nothing moved the animation is
// over: drop the timer, stop the music and raise the finished sound.
void sub_462F70(ctrl_t *pDlg)
{
  int bDone = 1;
  int i;

  for (i = 0; i < dword_66CA6C; i++) {
    scorerow_t *pRow = &sgScoreRows_g[i];
    LONG lTarget = pRow->lTotalTarget;
    LONG lAnim = pRow->lTotalAnim;
    LONG lStep = lTarget - lAnim;
    int k;

    if (lStep != 0) {
      bDone = 0;
      lStep /= 8;
      if (lStep < 4)
        lStep = 4;
      lStep += lAnim;
      if (lTarget < lStep)
        lStep = lTarget;
      pRow->lTotalAnim = lStep;
    }
    psnprintf(pRow->szTotal, 0x80, "%u", pRow->lTotalAnim / pRow->lTotalScale);
    ctrl_set_update(pRow->pCtrlTotal);
    for (k = 0; k < 3; k++) {
      LONG lBarTarget = pRow->lTarget[k];
      LONG lBarStep = lBarTarget - pRow->lAnim[k];

      if (lBarStep != 0) {
        bDone = 0;
        lBarStep /= 8;
        if (lBarStep < 4)
          lBarStep = 4;
        pRow->lAnim[k] += lBarStep;
        if (lBarTarget < pRow->lAnim[k])
          pRow->lAnim[k] = lBarTarget;
      }
      psnprintf(pRow->szBar[k], 0x80, "%u", pRow->lAnim[k] / pRow->lScale[k]);
      ctrl_set_update(pRow->pCtrlBar[k]);
    }
  }
  if (bDone) {
    timer_remove_node(pDlg, 0x22);
    stop_digital();
    sub_49E740(0xA9);
  }
}


// Sort the rows by rank, best first, with a selection sort that scans the
// global rows, and raise each column's maximum to every used row's value.
// The fields are read through pRow and the swap through pRows[i]: MSVC gives each
// its own pointer IV, the +0x18 fields from pRow and the +0 swap from i.
void sub_4630C0(scorerow_t *pRows, int nCount)
{
  int i = 0;
  if (nCount > 0) {
    scorerow_t *pRow = pRows;
    do {
      int nBest = score_row_rank(pRow, i);
      int nBestIdx = i;
      scorerow_t *pScan = &sgScoreRows_g[i + 1];
      for (int j = i + 1; j < 8; j++, pScan++) {
        int nRank = sub_4623C0(pScan, j);
        if (nRank > nBest) {
          nBest = nRank;
          nBestIdx = j;
        }
      }
      if (nBestIdx > i) {
        scorerow_t tmp = pRows[i];
        pRows[i] = pRows[nBestIdx];
        pRows[nBestIdx] = tmp;
      }
      if (pRow->bUsed == 1) {
        if (pRow->lValue[0] > qword_66B424[0]) qword_66B424[0] = pRow->lValue[0];
        if (pRow->lValue[1] > qword_66B424[1]) qword_66B424[1] = pRow->lValue[1];
        if (pRow->lValue[2] > qword_66B424[2]) qword_66B424[2] = pRow->lValue[2];
      }
      i++, pRow++;
    } while (i < nCount);
  }
}
