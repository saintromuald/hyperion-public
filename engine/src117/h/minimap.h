// minimap.h -- the interface of src117/lang/minimap.cpp: the minimap dialog and
// its image control -- the terrain-colour pass, the ping pool, the blink/refresh
// timers, the drag-scroll and targeting click handlers, and the border draw.
// Where the naming sheet has no name, an address keeps its sub_/word_/dword_
// spelling.
//
// Include after <stddef.h>.
#pragma once
#include "platform.h"  // LONG and ULONG

#include "bmp.h"
#include "dialog.h"
#include "rect16.h"
#include "gametype.h"   // IsTVBGame

// One entry of sgPings_g @0x671E98, the 16 minimap pings; the field widths are
// do_minimap_ping's stores.
struct minimap_ping_t {
  /*0x00*/ unsigned char active;
  /*0x01*/ unsigned char _unmapped01[3];
  /*0x04*/ ULONG x;
  /*0x08*/ ULONG y;
  /*0x0C*/ unsigned short timer;
  /*0x0E*/ unsigned char player;
  /*0x0F*/ unsigned char _unmapped0F;
};
static_assert(sizeof(minimap_ping_t) == 0x10, "minimap_ping_t  stride: sub_46EBA0 `add eax,10h` over 0x671E98..0x671F98");
static_assert(offsetof(minimap_ping_t, active) == 0x00, "minimap_ping_t::active  sub_46EBA0: cmp byte ptr [eax], 0h");
static_assert(offsetof(minimap_ping_t, x) == 0x04, "minimap_ping_t::x       do_minimap_ping: mov dword ptr [ecx+4h], eax");
static_assert(offsetof(minimap_ping_t, y) == 0x08, "minimap_ping_t::y       do_minimap_ping: mov dword ptr [ecx+8h], eax");
static_assert(offsetof(minimap_ping_t, timer) == 0x0C, "minimap_ping_t::timer   sub_46EA90: movzx ecx, word ptr [eax] (base 0x671EA4 = +0x0C)");
static_assert(offsetof(minimap_ping_t, player) == 0x0E, "minimap_ping_t::player  do_minimap_ping: mov byte ptr [ecx+0Eh], al");

// The argument-less callbacks parked in dword_671FC0 / dword_671FC4 and in the
// timer table: each is called with no argument set up and reads neither ECX nor
// EDX.
typedef void(__fastcall *PFN_VOID)(void);
typedef void(__fastcall *PFN_TIMER)(void);

// The function type behind PFN_TIMER, so timer_create's fourth parameter can be
// spelled `PFN_TIMER_FN *`. It is the same type, so the mangled name and the
// code are unchanged.
typedef void(__fastcall PFN_TIMER_FN)(void);

// Callees whose bodies are in other files.

// AllocBlackX -- oracle 0x417AD0. The black selection X dropped at a target.
void __fastcall AllocBlackX(int nX, int nY);

// IsPausedSingleplayer -- oracle 0x47CB70. No arguments.
int __fastcall IsPausedSingleplayer(void);

// ctxhelp_hide -- oracle 0x4191D0. No arguments.
void __fastcall ctxhelp_hide(void);

// game_hold_cursor -- oracle 0x448C50. The argument is sixteen bits unsigned:
// the body zero-extends it before its tail jump to cursor_set.
void __fastcall game_hold_cursor(WORD wCursor);

// sub_46CFD0 -- oracle 0x46CFD0. Clamps a raw cursor position into the minimap
// control's client area and scales it. The body writes only the low 16 bits of
// each, but 1.17's callers were compiled against a 4-byte pointee and narrow
// the result again after the call; declared `short *`, that narrowing is lost.
void __fastcall sub_46CFD0(LONG *px, LONG *py);

// sub_46E1E0 -- oracle 0x46E1E0. Raises the context help for a minimap control.
void __fastcall sub_46E1E0(ctrl_t *pCtrl);

// usel_click_target -- oracle 0x4BC820. The minimap passes 0 and 0xE4 as the
// two stack arguments. The coordinates are int: the caller zero-extends a WORD
// into each.
void __fastcall usel_click_target(int nX, int nY, intptr_t a3, int a4);

// IsAutoScroll -- oracle 0x49B7C0. A BYTE result: the caller tests AL.
unsigned char __fastcall IsAutoScroll(void);

// get_num_active_players -- oracle 0x477220. A BYTE result, compared unsigned.
unsigned char __fastcall get_num_active_players(void);

// btn_ctrl_pushbutton -- oracle 0x4C7500. The stock push-button event handler
// minimap_btn_ctrl_fcn tail-calls; a plain PFN_CTRL_EVT.
int __fastcall btn_ctrl_pushbutton(ctrl_t *pCtrl, dlgevt_t *pEvent);

// minimap_btn_command / minimap_btn_activate -- oracle 0x46D060 / 0x46D130.
// Both read ECX only, so they take the control alone.
void __fastcall minimap_btn_command(ctrl_t *pCtrl);
// Hyperion: the minimap's Tab for Hyperion's terrain key (minimap.cpp):
// the terrain, or with Shift held the units' colouring mode, as 1.17's Tab.
void PortMinimapTerrainKey(void);
void __fastcall minimap_btn_activate(ctrl_t *pCtrl);

// cell_set_pos -- oracle 0x40B450. mou_cager -- oracle 0x4CE850.
void __fastcall cell_set_pos(int nX, int nY);
void __fastcall mou_cager(intptr_t nMode);

// status_get_console_art -- oracle 0x4A7C40. sub_46D850 -- oracle 0x46D850.
void __fastcall status_get_console_art(ctrl_t *pDlg);
void __fastcall sub_46D850(ctrl_t *pDlg);

// sub_46D3F0 / sub_46D510 -- oracle 0x46D3F0 / 0x46D510. Argument-less minimap
// redraw stages: no register input, and every caller discards EAX.
void __fastcall sub_46D3F0(void);
void __fastcall sub_46D510(void);

// sub_46D240 -- oracle 0x46D240, the callback timer_create(pDlg, 5, 500,
// sub_46D240) installs. timer_update passes every callback the owning dialog in
// ECX and the timer id in EDX, which this body forwards to timer_remove_node;
// PFN_TIMER_FN, nullary, is wrong for the whole timer family.
void __fastcall sub_46D240(ctrl_t *pDlg, int nTimerId);

// timer_create -- oracle 0x4C5960: ECX the owner dialog, EDX the timer id, then
// the interval and the callback on the stack. The id is a WORD, as the body
// stores it. timer_remove_node -- oracle 0x4C58F0.
void __fastcall timer_create(ctrl_t *pDlg, dlg_word_t wId, int nInterval, PFN_TIMER_FN *pfn);
void __fastcall timer_remove_node(ctrl_t *pDlg, int nId);

// minimap_image_draw -- oracle 0x46E990, 244 B. Installed in the control's +0x2E
// draw slot and closing `ret 8h`: an ordinary PFN_CTRL_DRAW.
void __fastcall minimap_image_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// minimap.cpp's own functions; the comment at each body gives its address.
void __fastcall sub_46C8E0(unsigned char *pa, unsigned char *pb);
void __fastcall sub_46C900(void);
void __fastcall sub_46C920(void);
void __fastcall sub_46C990(void);
void __fastcall minimap_check_scroll(void);
void __fastcall DrawMinimap(void);
void __fastcall sub_46E140(unsigned char bFull);
void __fastcall sub_46E840(void);
void __fastcall minimap_force_redraw(void);
void __fastcall sub_46E8D0(void);
void __fastcall do_minimap_ping(ULONG nX, ULONG nY, unsigned char bPlayer);
void __fastcall minimap_btn_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);
int __fastcall minimap_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall minimap_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall minimap_dlg_deactivate(ctrl_t *pDlg);
void __fastcall minimap_image_ctrl_activate(ctrl_t *pCtrl);
void __fastcall minimap_dlg_activate(ctrl_t *pDlg);
void __fastcall sub_46EA90(void);
void __fastcall minimap_dlg_draw(ctrl_t *pDlg, int nXOfs, int nYOfs, rect16_t *prcClip);
void __fastcall sub_46EBA0(void);
void __fastcall minimap_image_ctrl_target_mousedown(dlgevt_t *pEvent);
void __fastcall minimap_image_ctrl_mousemoved(ctrl_t *pCtrl);

// minimap_init -- oracle 0x46E260 (436 B). minimap_deactivate -- oracle 0x46E1A0
// (57 B). Both nullary and void.
void __fastcall minimap_init();
void __fastcall minimap_deactivate();

// do_minimappreview_dlg -- oracle 0x46EB60 (50 B). Nullary; its one caller
// (gluChat_hover_minimap_preview) discards EAX.
void __fastcall do_minimappreview_dlg();

// The per-player unit-dot passes (ECX = the player) and the dot filler they
// share. The filler's coordinates are 32-bit and compared unsigned; its last
// argument is a BYTE.
void __fastcall sub_46E490(int nPlayer);
void __fastcall sub_46E5B0(int nPlayer);
void __fastcall sub_46E700(int nPlayer);
void __fastcall sub_46CB80(unsigned nX, unsigned nY, BYTE bWidth, BYTE bHeight,
                           BYTE bColor, BYTE bFlag);

// The dialog and control procedures, and the two handlers reached only through
// a control's slot. dialog.h's PFN_CTRL_EVT fixes the two return types.
int  __fastcall minimap_image_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);
int  __fastcall minimap_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);
void __fastcall minimap_image_ctrl_mousedown(ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall minimap_ctxhelp_terrain(ctrl_t *pCtrl);

// oracle 0x46CA20. Nullary; the 2:1 terrain stage sub_46D850 installs in
// dword_671FC0.
void __fastcall sub_46CA20(void);
// oracle 0x46CD10. Nullary; folds the visibility masks for one minimap pass.
void __fastcall sub_46CD10(void);
// oracle 0x46CEA0. Nullary; the 1:2 fog stage sub_46D850 installs in
// dword_671FC4.
void __fastcall sub_46CEA0(void);
// oracle 0x46CDC0. Nullary; the 2:1 fog stage sub_46D850 installs in
// dword_671FC4.
void __fastcall sub_46CDC0(void);

// gamemap_center_on_mtx is declared in gamemap.h.
#include "gamemap.h"
