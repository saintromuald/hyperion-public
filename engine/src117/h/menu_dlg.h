// menu_dlg.h -- declarations for src117/lang/menu_dlg.cpp, the menu dialog's
// button sizing and title placement helpers. Free functions are __fastcall
// through /Gr and are declared without the keyword.

#ifndef OC117_MENU_DLG_H
#define OC117_MENU_DLG_H

#include "platform.h"
#include "dialog.h"

// 0x46C790 dlg_level_init (8 B) -- nullary; clears the dialog nesting byte.
void dlg_level_init();

// 0x46C7A0 menu_btn_fcn (42 B) -- a PFN_CTRL_EVT whose address
// big_menu_btn_fcn stores into +0x2A. It reads EDX as a dlgevt_t and ECX as the
// ctrl_t whose nIndex it publishes, returns 1 on the handled path and otherwise
// tail-jumps to btn_ctrl_pushbutton.
BOOL menu_btn_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x46C7D0 big_menu_btn_fcn (78 B) -- a PFN_CTRL_EVT, reached only by address.
// It reads EDX as a dlgevt_t (wTag at +0x0C, dwId at +0x00) and ECX as the
// ctrl_t it writes +0x18, +0x24 and +0x2A of, returns 1 on the handled path and
// otherwise tail-jumps to btn_ctrl_pushbutton.
BOOL big_menu_btn_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x46C820 set_btn_size (28 B) -- `ret 4h`; ECX is dereferenced and EDX read as
// 16 bits. The index is unsigned: the body widens it with movzx before scaling
// it into sgwBtnFrames.
void set_btn_size(ctrl_t *pCtrl, WORD wFrame, DWORD dwFlags);

// 0x46C840 set_size_enter (47 B) and 0x46C870 set_size_escape (47 B). `ret 4h`;
// EDX is read as 16 bits and ECX is only forwarded to ctrl_find_ndx.
void set_size_enter(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);
void set_size_escape(ctrl_t *pDlg, WORD wFrame, DWORD dwFlags);

// 0x46C8A0 title_position (55 B) -- ECX only, the owning dialog at every call
// site (the five `*_dlg_fcn` bodies in gluPopup.cpp and gluModem.cpp). EAX is
// never set.
void title_position(ctrl_t *pDlg);

#endif  // OC117_MENU_DLG_H
