// gluRdy.h -- declarations for src117/lang/gluRdy.cpp, the mission briefing
// (the "ready room") screen: its dialog init, command and deactivate handlers,
// the speaking-portrait control, the objectives text, the briefing-trigger
// list and the wait and advance timers
//
// Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default. The declarations are grouped by argument shape.

#ifndef OC117_GLURDY_H
#define OC117_GLURDY_H

#include "platform.h"
#include "dialog.h"
#include "bmp.h"      // bmp_t / bmpsrc_t for bmp_create + bmp_mask_rect
#include "gds_bmpmasre.h"

// One ctrl_t * in ECX, the dialog: glurdy_dlg_init forwards ECX into
// ctrl_find_ndx and stores a handler at +0x4A, both ctrl_t operations.

void glurdy_dlg_init(ctrl_t *pDlg);
// Returns a value: the oracle materialises EAX on all three exits, 1 on two of
// them and glues_transition_out's widened BYTE on the third. MSVC mangles the
// return type, so a header and a definition that disagree are two symbols.
BOOL glurdy_dlg_command(ctrl_t *pDlg);
void glurdy_dlg_deactivate(ctrl_t *pDlg);
void glurdy_update_objectives(ctrl_t *pDlg);
void sub_460A70(ctrl_t *pDlg);
void glurdy_reset_briefing(ctrl_t *pDlg);
void glurdy_run_trigger(ctrl_t *pDlg);
void glurdy_stop_briefing(ctrl_t *pDlg);
void glurdy_wait_players(ctrl_t *pDlg);

// 0x4607E0. ECX is the 0x960-byte trigger record. The body never reads EDX,
// but maphdr_MBRF, the one caller, clears it, so the second argument stays.
int addBriefingTrigger(void *pTrigger, int nFlags);

// glurdy_start_wait_timer's ECX is pushed as timer_create's stack argument
// rather than used as a dialog, so it is the interval, an int.
void glurdy_start_wait_timer(int nInterval);

// glurdy_text_scroll is PFN_TIMER-shaped and only bumps a ctrl_t counter
// before jumping to ctrl_set_update. Its address is taken twice.
void glurdy_text_scroll(ctrl_t *pCtrl);

// The timer callbacks, (ctrl_t *pDlg, int nId). Each ends by forwarding its
// pair untouched to timer_remove_node, which minimap.h derives as
// (ctrl_t *, int). Each has its address taken and no direct call site, so the
// arity is a lower bound.

void glurdy_wait_timer_finished_cb(ctrl_t *pDlg, int nId);
void glurdy_text_scroll_start(ctrl_t *pDlg, int nId);
void glurdy_text_timeout(ctrl_t *pDlg, int nId);

// The portrait family, whose BYTE is a portrait slot index. The width shows in
// the zero-extend at the entry of all three, and the slot is biased by 0x0F
// into ctrl_find_ndx, so it indexes the briefing dialog's portrait controls.
// Spelling any of them `int` would be the wrong width.

void glurdy_hide_portrait(BYTE bSlot);
void glurdy_show_portrait(WORD wUnitId, BYTE bSlot);
void glurdy_talking_portrait(BYTE bSlot, int nArg1);

// Two 32-bit register arguments.

// nArg0 is a ctrl_t *, not an int: it feeds ctrl_find_ndx's ECX and takes
// stores at +0x26 and +0x4A. Both are four bytes, so this is for the reader --
// but a wrong type here is what makes a later author write a cast rather than
// a call.
void sub_460A20(ctrl_t *pDlg, int nArg1);
// nArg0 is a string (pushed as font_adjust's third argument and null-tested);
// nArg1 is unsigned (`cmp edi, 0FA0h / jbe` and two unsigned `div ecx`). The
// unsignedness IS byte-visible -- a signed spelling changes the divide.
void glurdy_text_display(const char *pszText, unsigned int nDuration);

// glurdy_draw_text pops two stack words and reads neither, and never reads EDX
// either -- but /Gr fills EDX before the stack, so a second register argument
// exists and the callee ignores it. It is a callback, so the arity is a lower
// bound and only its arguments' footprints are derived.
void glurdy_draw_text(ctrl_t *pCtrl, int nArg1, int nArg2, int nArg3);

// Nullary.

// `call stop_music / mov ecx, 16h / jmp snd_play_music` -- the track id is a
// literal here and a parameter in glurdy_set_music, hence the two bodies.
void glurdy_set_menu_music();
void glurdy_set_music(int nTrack);

void glurdy_show_skip_tutorial();


// External callees this TU reaches that no other header of ours declares, each
// declared once. If one of those TUs opens later, move the declaration rather
// than copying it.

// brfport.cpp -- the briefing portrait control.
void brfport_hide_portrait(ctrl_t *pCtrl);
void brfport_show_portrait(ctrl_t *pCtrl, WORD wPortrait);
void brfport_talking_portrait(void *pArg0, int nArg1);

// brfact.cpp -- the briefing action/trigger runner.
void brfact_clear_waiting();

// stat_txt / unit portrait lookup: reads a WORD unit id.
int get_unit_portrait(WORD wUnitId);

// net_glue.cpp.
void sub_475D10();

// music.cpp / snd.cpp.
void stop_music();
void snd_play_music(int nTrack);
void play_music(int nTrack);
// Hyperion: stop_music's fade on the window's timer, without its wait, then nTrack
// (music.cpp), for Hyperion's screens, which keep drawing while it fades.
void PortMusicChange(int nTrack);
// Hyperion: the track playing, its index in the songs' table, or -1 (the live channel's status).
int PortMusicPlaying(void);

// glue portrait helpers reached by tail jmp.
void sub_4502E0(intptr_t nArg0);
void sub_4504C0(intptr_t nArg0);

// brfact_RunAction  -- `lea ecx, [edi+8h] / call ...` off a TRIGGERNODE*, the
//   result consumed by `test eax,eax` => int, one ECX argument.
// briefing_port_free -- `call ...` with no argument setup in front of it.
// sub_476B20        -- `lea ecx, [ebp-4h] / call ...`, i.e. one ECX
//   out-pointer to a 4-byte local; the result is consumed by `test eax,eax`.
int brfact_RunAction(void *pConditions);
void briefing_port_free();
int sub_476B20(int *pnOut);


// gluRdy.cpp's own external callees. snd_end_streamed is absent because
// src117/h/victory.h declares it and the merged TU includes that header, and
// font_set and font_adjust because dlg_draw.h declares font_set while
// gluRdy.cpp declares font_adjust in the .cpp, as dlg_draw.cpp does.
void packet_briefing_start(int nStart);
void bmp_create(bmp_t *pBmp, const char *pszFile, int nLine);
void briefing_port_init(void);
BYTE glues_transition_out(ctrl_t *pDlg);

#endif  // OC117_GLURDY_H
