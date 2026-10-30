// statdata.h -- the interface of src117/lang/statdata.cpp: the status panel
// (oracle 0x4A19E0 to 0x4A30xx), the lower-left dialog that shows the selected
// unit's portrait, hit points, shields, energy, resource counts, build and
// research progress bars, transport slots and the production queue. Its
// controls are found by index and shown or hidden in sibling runs, so many of
// its bodies walk ctrl_t::pNext.
//
// Include after <stddef.h> (dialog.h's static_asserts use offsetof) and after
// the platform scalar scaffold.
#pragma once

#include "platform.h"  // OC_X86_LAYOUT
#include "dialog.h"
#include "dlg_ctrl.h"
#include "bmp.h"

// Six bodies here set EDX and never ECX before `call ctrl_find_ndx`, which
// takes (ctrl_t *, short): they pass their own incoming ECX through, so each
// takes the node as its first parameter. The TU's three shapes are
// void f(ctrl_t *), the PFN_CTRL_EVT int f(ctrl_t *, dlgevt_t *), and the
// PFN_CTRL_DRAW void f(ctrl_t *, int, int, rect16_t *), `ret 8h`, whose EDX
// several draw bodies never read.

// The panel's type selector. 0x4A1EC0 returns AL, so BYTE; 0x4A2440 is the
// setter, taking CL.
BYTE __fastcall statdata_get_type();
void __fastcall statdata_set_type(BYTE bType);

// Marks the panel dirty and drops the cached hover target.
void __fastcall request_statdata_update();

// Hides every sibling in the node's run. The hop to the children happens only
// when wType is 0, ctrl_find_ndx's own first two instructions inlined.
void __fastcall statdata_clr_all(ctrl_t *pCtrl);

// Hides the run that follows the node with index -7. The find result is used
// with no null check, as in 1.17.
void __fastcall statdata_clr_stats(ctrl_t *pCtrl);

// Writes a progress value into the +0x24 WORD of the node with the given index,
// then shows it and marks it for redraw. Both registers pass through to
// ctrl_find_ndx; the value comes on the stack (`ret 4h`).
void __fastcall statdata_set_progress(ctrl_t *pCtrl, short nIndex,
                                      short nValue);

// Six bodies of one shape with two constants: each shows the nodes of the
// sibling run from a start index, stopping after a stop index, tested at the
// loop's bottom so the stop node is shown too. Start and stop:
// statdata_show_progress -0x28 / 0x11, statdata_show_progress_self -0x1E / 0x0D,
// statdata_show_stats -0x14 / 0x0C, statdata_show_resources -0x0A / -0x0D; the
// other two are in the .cpp.
void __fastcall statdata_show_progress(ctrl_t *pCtrl);
void __fastcall statdata_show_progress_self(ctrl_t *pCtrl);
void __fastcall statdata_show_progress_queue(ctrl_t *pCtrl);
void __fastcall statdata_show_progress_research(ctrl_t *pCtrl);
void __fastcall statdata_show_resources(ctrl_t *pCtrl);
void __fastcall statdata_show_stats(ctrl_t *pCtrl);

// Anchors the context-help tooltip to the node, in the parent's coordinates
// when the node is not top-level. The X term is the control's rc.right.
void __fastcall statdata_ctx_show(ctrl_t *pCtrl, intptr_t nParam);

// Tears down both progress-bar bitmaps. The guard reads bmp_t::pPixels (+4) and
// the call takes the object, so 0x6AAB1C / 0x6AAB24 are fields, not globals.
// The control parameter is unused; it gives the caller 1.17's registers.
void __fastcall statdata_progressbar_deactivate(ctrl_t *pCtrl);

// The progress bar's event handler (0x4A1D40).
int __fastcall statdata_progressbar_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// Installs its own draw handler and the type-default event handler on first
// paint, then tail-dispatches through gCtrlEvtFcnTbl.
int __fastcall statdata_queue_arrow_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// PFN_CTRL_DRAW handlers (`ret 8h`) that read only the control.
void __fastcall statdata_icon_info_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                        rect16_t *prcClip);
void __fastcall statdata_btn_queue_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                        rect16_t *prcClip);
void __fastcall statdata_queue_arrow_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                          rect16_t *prcClip);
void __fastcall statdata_progressbar_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                          rect16_t *prcClip);

// These two return void: every exit is a bare ret or a tail jump into a void
// callee, and both callers, statdata_dlg_fcn and statdata_icon_btn_ctrl_fcn,
// discard EAX. Both consult IsGamePaused first.
void __fastcall statdata_dlg_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);
void __fastcall statdata_icon_btn_mouse_moved(ctrl_t *pCtrl, dlgevt_t *pEvent);

// ECX and a bare ret; large string buffers in the frame.
void __fastcall statdata_ctx_trans(ctrl_t *pCtrl);
void __fastcall statdata_ctx_research(ctrl_t *pCtrl);

// No parameters: nothing is read before the first global load.
void __fastcall statdata_update_hover();

// The transport slot button's event handler.
int __fastcall statdata_transport_btn_ctrl_fcn(ctrl_t *pCtrl,
                                               dlgevt_t *pEvent);

// Panel lifetime, the per-frame update and the panel dialog's event handler.
void __fastcall statdata_init();
void __fastcall statdata_deactivate();
void __fastcall statdata_update();
int __fastcall statdata_dlg_fcn(ctrl_t *pDlg, dlgevt_t *pEvent);

// Icon buttons: the +0x26 payload allocator and the right-click command.
void __fastcall statdata_icon_btn_activate(ctrl_t *pCtrl);
void __fastcall statdata_slot_cmd(ctrl_t *pCtrl);
int __fastcall statdata_icon_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The multi-selection buttons.
// Hyperion: the frame of the command buttons' GRP a selected unit's box is drawn
// in (statdata_select_btn_draw: 0x1F a hallucination, 0x13 a unit of type flag 0x40, 0x19 one a
// parasite rides, else 0x0D); and a click on its wireframe (statdata_select_btn_command): that
// unit alone, with Shift every other unit selected, with Ctrl every selected unit of its type,
// over the whole selection. Both for the HUD's selection grid (port_gpu_hud.cpp, port_hudinput.cpp).
class CUnit;
int StatDataBoxFrame(const CUnit *pUnit);
void StatDataSelectAmong(CUnit *pUnit);
void __fastcall statdata_select_btn_command(ctrl_t *pCtrl);
void __fastcall statdata_select_btn_draw(ctrl_t *pCtrl, int nXOfs, int nYOfs,
                                         rect16_t *prcClip);
int __fastcall statdata_select_btn_ctrl_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// The button art: the frame, the icon payload, and a transport passenger.
void __fastcall statdata_slot_frame_draw(ctrl_t *pCtrl);
void __fastcall statdata_slot_icon_draw(ctrl_t *pCtrl);
void __fastcall statdata_transport_btn_draw(ctrl_t *pCtrl, int nXOfs,
                                            int nYOfs, rect16_t *prcClip);

// Context-help builders for the training slots and the status icons.
void __fastcall statdata_ctx_training(ctrl_t *pCtrl);
void __fastcall statdata_ctx_status(ctrl_t *pCtrl);

// ctxhelp_show -- oracle 0x418F60, in CtxtHelp.cpp. Three parameters, `ret 4h`:
// the coordinates in ECX and EDX and a dword on the stack, passed through
// opaquely.
void __fastcall ctxhelp_show(int nX, int nY, intptr_t nParam);

// Installs the progress-bar draw handler at +0x2E and loads the bar images.
void __fastcall statdata_progressbar_activate(ctrl_t *pCtrl);

// statdata_hover_update -- oracle 0x4A2380. Two register parameters, the dialog
// and the node the help is for, as both call sites pass them; void. Not
// statdata_update_hover (0x4A2B70, nullary, above).
void __fastcall statdata_hover_update(ctrl_t *pDlg, ctrl_t *pCtrl);

// grhsize_t -- grh_get_header's second out-parameter: two WORDs, the group's
// width and height. It has no dependencies, so this header carries it without
// including grp.h.
struct grhsize_t {
  /*0x00*/ WORD wWidth;   // GROUP +0x02
  /*0x02*/ WORD wHeight;  // GROUP +0x04
};
static_assert(sizeof(grhsize_t) == 0x04, "grhsize_t size     grh_get_header writes exactly two words through it");
static_assert(offsetof(grhsize_t, wWidth)  == 0x00, "grhsize_t::wWidth  grh_get_header: mov word ptr [edx], ax");
static_assert(offsetof(grhsize_t, wHeight) == 0x02, "grhsize_t::wHeight grh_get_header: mov word ptr [edx+2h], ax");

// statdata_icon_t -- the ctrl_t +0x26 payload of an icon button, 12 bytes, what
// statdata_icon_btn_activate allocates.
struct statdata_icon_t {
  // A GRP handle, handed to grh_get_header; the pointee is not named.
  /*0x00*/ void *pGrp;
  // The frame handed to grh_get_header with the handle.
  /*0x04*/ WORD wFrame;
  // What the icon stands for: 3 a unit, 4 a tech, 5 an upgrade, and
  // statdata_ctx_status's 0 to 3; other values are not known.
  /*0x06*/ WORD wKind;
  // The id wKind's table is indexed by: a units.dat type for 3, a techdata row
  // for 4, an upgrades row for 5.
  /*0x08*/ WORD wParam;
  // The rest of the 12 bytes; nothing in this TU touches it.
  /*0x0A*/ BYTE _unmapped0A[2];
};

OC_X86_LAYOUT(sizeof(statdata_icon_t)         == 0x0C, "statdata_icon_t size    statdata_icon_btn_activate: push 0Ch / call SMemAlloc / mov [esi+26h], eax");
static_assert(offsetof(statdata_icon_t, pGrp)   == 0x00, "statdata_icon_t::pGrp   statdata_slot_icon_draw: mov ecx, dword ptr [ecx]");
OC_X86_LAYOUT(offsetof(statdata_icon_t, wFrame) == 0x04, "statdata_icon_t::wFrame statdata_slot_icon_draw: mov dx, word ptr [ecx+4h]");
OC_X86_LAYOUT(offsetof(statdata_icon_t, wKind)  == 0x06, "statdata_icon_t::wKind  statdata_ctx_training: cmp word ptr [edx+6h], 3h");
OC_X86_LAYOUT(offsetof(statdata_icon_t, wParam) == 0x08, "statdata_icon_t::wParam statdata_ctx_research: movzx eax, word ptr [edi+8h]");

// The ctrl_t +0x26 payload of a multi-selection button: SMemAlloc(8) in
// statdata_select_btn_ctrl_fcn.
class CUnit;
struct statdata_select_t {
  /*0x00*/ CUnit *pUnit;
  /*0x04*/ WORD wFrame;   // the unit type, the wireframe frame
  /*0x06*/ BYTE _unmapped06[2];
};
OC_X86_LAYOUT(sizeof(statdata_select_t) == 0x08, "statdata_select_t size");
static_assert(offsetof(statdata_select_t, pUnit) == 0x00, "statdata_select_t::pUnit");
OC_X86_LAYOUT(offsetof(statdata_select_t, wFrame) == 0x04, "statdata_select_t::wFrame");
