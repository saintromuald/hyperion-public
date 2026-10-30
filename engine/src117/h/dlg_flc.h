// dlg_flc.h -- dlg_flc.cpp's functions and flc_t: the FLC dialog control, whose
// video elements ramp their blend level on hover. The campaign screens and
// gluConn install its event handlers. The functions are __fastcall through /Gr
// and declared without the keyword.

#ifndef OC117_DLG_FLC_H
#define OC117_DLG_FLC_H

#include "platform.h"
#include "bmp.h"
#include "dialog.h"

// 0x4C82F0. A PFN_CTRL_EVT handler in the campaign screens' event tables
// (sCmpgnEvtFcnTbl_g, sExpCmpgnEvtFcnTbl_g).
int ctrl_flc(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C83B0. The most common entry of the same tables, and two slots of gluConn's
// own; the same PFN_CTRL_EVT shape.
int ctrl_btnglow_flc(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C89B0. ECX only, returns nothing.
void flc_anim_update_fn(ctrl_t *pCtrl);

// 0x4C8150. PFN_CTRL_DRAW's shape: four arguments, two on the stack.
void draw_flc(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *pClip);

// One animated element of an FLC control. A wType == 0x0E node's +0x42 slot
// heads a list of these, not of ctrl_t nodes.
#pragma pack(push, 1)
struct flc_t {
  /*0x00*/ flc_t *pNext;
  // Bit 0 animates the element on hover, bit 1 selects the 30 rest level,
  // bit 2 is folded into SVidPlayBegin's flag word, bit 4 marks it live.
  /*0x04*/ dlg_dword_t dwFlags;
  // The current blend level, 0..100.
  /*0x08*/ dlg_word_t wLevel;
  /*0x0A*/ const char *pszFile;
  /*0x0E*/ void *hVid;
  /*0x12*/ dlg_word_t wXOfs;
  /*0x14*/ dlg_word_t wYOfs;
  /*0x16*/ bmp_t bmp;
};
#pragma pack(pop)

OC_X86_LAYOUT(sizeof(flc_t)              == 0x1E, "flc_t size       bmp_t at +0x16 is the last thing any body touches");
static_assert(offsetof(flc_t, pNext)     == 0x00, "flc_t::pNext     flc_remove_timers walk");
OC_X86_LAYOUT(offsetof(flc_t, dwFlags)   == 0x04, "flc_t::dwFlags   ctrl_flc_init or");
OC_X86_LAYOUT(offsetof(flc_t, wLevel)    == 0x08, "flc_t::wLevel    flc_hover_update ramp");
OC_X86_LAYOUT(offsetof(flc_t, pszFile)   == 0x0A, "flc_t::pszFile   flc_init_flc SVidPlayBegin arg0");
OC_X86_LAYOUT(offsetof(flc_t, hVid)      == 0x0E, "flc_t::hVid      flc_init_flc SVidPlayBegin out");
OC_X86_LAYOUT(offsetof(flc_t, wXOfs)     == 0x12, "flc_t::wXOfs     flc_draw_flc");
OC_X86_LAYOUT(offsetof(flc_t, wYOfs)     == 0x14, "flc_t::wYOfs     flc_draw_flc");
OC_X86_LAYOUT(offsetof(flc_t, bmp)       == 0x16, "flc_t::bmp       bmp_create / bmp_destroy take its address");

// Hyperion: the FLC control is wType 0x0E, and these are the items its
// pChildren heads in a dialog file, 0x1E bytes each, which dlg_load_ctrl relocates
// through dialog.h's dlgitem_t, a view of their first fields; PortDialogNative
// (src117/port/data/port_formats.cpp) writes them as flc_t.
static_assert(offsetof(dlgitem_t, pNext) == offsetof(flc_t, pNext) &&
              offsetof(dlgitem_t, pText) == offsetof(flc_t, pszFile),
              "dlgitem_t is flc_t's head");

// The rest of dlg_flc.cpp, in address order.

// 0x4C8200  relocate the payload rect into dialog coordinates, start the
// elements and install draw_flc.
void ctrl_flc_init(ctrl_t *pCtrl);

// 0x4C82B0  the handler for an FLC that only tracks hover.
int ctrl_flc_only_mouseover(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C8470  stop every element and put the payload rect back.
void ctrl_flc_deactivate(ctrl_t *pCtrl);

// 0x4C84F0  arm the 30 ms animation timer on every FLC child of a dialog.
void flc_animate(ctrl_t *pDlg);

// 0x4C8520  blit one element, clipped against the control and the caller rect.
void flc_draw_flc(ctrl_t *pCtrl, flc_t *pItem, int nXOfs, int nYOfs,
                  rect16_t *prcClip);

// 0x4C8650  draw the control's caption at the given blend level. The third
// argument is never read in 1.17; both callers pass their clip rect.
void flc_draw_text(ctrl_t *pCtrl, int nLevel, rect16_t *prcClip);

// 0x4C8730  the draw handler ctrl_btnglow_flc_evn installs at +0x2E.
void flc_draw_text_fcn(ctrl_t *pCtrl, int nXOfs, int nYOfs, rect16_t *prcClip);

// 0x4C8760  ramp the control's and every element's blend level toward or away
// from the hover target.
void flc_hover_update(ctrl_t *pCtrl);

// 0x4C8860  size the element's bitmap from the video and start it looping.
void flc_init_flc(flc_t *pItem);

// 0x4C8970  hit test against the payload rect, returning the locatable bit.
int flc_locate(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C8A40  track the pressed bit against the given rect.
void flc_mouse_update(ctrl_t *pCtrl, dlgevt_t *pEvent, rect16_t *prc);

// 0x4C8AB0  ctrl_flc's wTag 0x0E arm.
int ctrl_flc_evn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4C8BC0  release: drop the pressed bit and tell the dialog.
void flc_clicked(ctrl_t *pCtrl);

// 0x4C8C00  ctrl_btnglow_flc's wTag 0x0E arm.
int ctrl_btnglow_flc_evn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_DLG_FLC_H
