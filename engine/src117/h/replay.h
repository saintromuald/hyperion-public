// replay.h -- declarations for src117/lang/replay.cpp: replay recording and
// playback, the replay header and slot blob, the playback speed orders, and the
// Load Replay dialog.
//
// Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default.

#ifndef OC117_REPLAY_H
#define OC117_REPLAY_H

#include "platform.h"
#include "dialog.h"

// The accessors over the replay header blob, nullary or with one register
// argument.

int replay_get_seed();
void replay_set_seed(int nSeed);
int replay_get_initial_game_speed();
int replay_get_max_players();
int replay_get_progress();

// The two halves of one 8-byte blob at 0x67CF11, copied in and out whole.
void replay_get_selectable_races(void *pOut);
void replay_set_selectable_races(const void *pIn);

// The 8-dword player colour block (sgReplaySlotMap_g).
void get_replay_player_colors(void *pOut);

// The 12 x 36-byte player slot records.
void replay_assign_player_slots(void *pOut);

// GetReplayMission is WORD: it loads a 16-bit word and its one caller uses 16
// bits of it.
WORD GetReplayMission();
int replay_get_total_frames();

int replay_dlg_get_mult();
void replay_set_total_frames(int nTotalFrames);
int replay_dlg_elapsed_seconds();

// The playback speed orders, and the queries that enable their buttons. The
// bf_* trio are __stdcall with one stack argument (`ret 4h`).
void order_replay_slowdown();
void order_replay_speedup();
void order_replay_play();
BOOL __stdcall bf_replay_slowdown(int nArg0);
BOOL __stdcall bf_replay_speedup(int nArg0);
BOOL __stdcall bf_replay_play(int nArg0);

// Sets the playback speed. bPaused is the third argument, on the stack and
// popped by the callee (`ret 4h`); its call site passes it as a byte.
void __fastcall replay_set_speed(DWORD dwSpeed, DWORD dwMult, BYTE bPaused);

void sub_480E00(int nArg0);
void replay_init_speed();
void sub_480FB0();

// The replay list dialog. sub_481300 is the callback sub_4803D0 calls with each
// replay's file name, its extension stripped.
void __stdcall sub_481300(const char *pszItem);
void replay_dlg_init(ctrl_t *pDlg);
void replay_dlg_activate(ctrl_t *pDlg);
void replay_dlg_deactivate(ctrl_t *pDlg);
int replay_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL replaydlg_btn_delete_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL replaydlg_edit_name_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
BOOL replaydlg_listbox_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
// The parameter is the list box control, not the dialog.
void replaydlg_listbox_ctrl_init(ctrl_t *pListbox);
// Its one caller, replaydlg_edit_name_fcn, passes its own two arguments on.
void replaydlg_edit_evn_char(ctrl_t *pCtrl, dlgevt_t *pEvent);
void sub_481450(ctrl_t *pListbox);
int ask_replay_dlg_arg(char *pszName, int nErrId);
int do_replay_dlg(int nArg0, int nArg1);

// Replay file I/O. pszWhat is compared with each rep_lb_types_g entry's szName.
BOOL replay_lb_show(char *pszWhat);
BOOL replay_lb_hide();
BOOL copy_lastreplay_to(const char *pszName);
// BOOL, not void: the failure path returns 0, and the pfdelete call's result is
// returned.
BOOL delete_replay(char *pszName);

// Hyperion: a LAN match's replay as it is played (port_lanfeed.cpp): its
// parts without the commands, taken from the game, and a replay written of parts and a record.
BOOL replay_live_parts(void *pHeader, void **ppChk, DWORD *pdwChk, DWORD *pdwMagic, const char *pszHxPath);
void replay_live_free(void *pChk);
BOOL replay_write_parts(const char *pszPath, DWORD dwMagic, const void *pHeader, const void *pRecord, int nRecord,
                        const void *pChk, DWORD dwChk, const void *pHx, DWORD dwHx);

// pszName goes into the path; bOverwrite asks to replace an existing file.
// Returns 1 or 0, or -1 when the file exists and cannot be deleted.
int write_replay(const char *pszName, BOOL bOverwrite);
// pszName is compared with "LastReplay", then packed into the path.
int sub_4802C0(char *pszName);
// Enumerates the replay directory and calls pfnAddItem (sub_481300) once per
// entry.
void sub_4803D0(void(__stdcall *pfnAddItem)(const char *));
void sub_480540(void *pArg0);
void replay_assign_gamedata(void *pHeaderOut, void *pSlotsOut);
int LoadReplayData(const char *pszName, int *pbIsExpansion);
void sub_4807F0(void *pArg0);
// pSlotMap points at eight dwords.
void replay_set_gamedata(const void *pHeaderIn, const void *pSlotsIn,
                const DWORD *pSlotMap);
// BOOL with explicit returns, though every caller ignores the result: as void
// the guard's branch inverts and the body is not 1.17's.
BOOL map_free();
void sub_480BD0(char *pszBuffer, DWORD cchBuffer);
// pszText may be NULL.
int parse_replay_command(char *pszText);

// 1.17 also inlines strip_trailing into replay_dlg_deactivate, where its loop
// is written out.
void strip_trailing(char *pszText);

#endif  // OC117_REPLAY_H
