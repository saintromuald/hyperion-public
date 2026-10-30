// videodlg.h -- declarations for src117/lang/videodlg.cpp, oracle
// 0x4BE5C0..0x4BE935: the video options dialog, the three control handlers it
// installs, and the pair that reads the settings out of the registry at startup
// and writes them back on the way out. Free functions are __fastcall through
// /Gr and are declared without the keyword. do_video_dlg is declared in
// opt_dlgs.h, with the rest of the options menu.

#ifndef OC117_VIDEODLG_H
#define OC117_VIDEODLG_H

#include "platform.h"
#include "dialog.h"

// 0x4BE5F0. The colour-cycling checkbox. It ends in a tail jump to
// btn_ctrl_checkbox and returns its int.
int video_dlg_colorcycle_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4BE640. Commits or discards the three pending settings and leaves the
// dialog. ECX is the dialog, forwarded to option_dlg_cmd and, once the dialog
// level reaches zero, to dlg_deactivate. The cancel path is the one that
// restores the three banked settings, not the one that deactivates.
void video_dlg_set_options(ctrl_t *pDlg);

// 0x4BE6A0. The dialog's event handler, which do_video_dlg hands to
// modeless_dlg_startup.
int video_dlg_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4BE790. Reads gamma, resolution and the playback option out of the
// registry; init.cpp is its only caller.
void init_video_opts();

// 0x4BE820. The portraits radio group. Tails into btn_ctrl_radiobutton.
int video_dlg_portraits_radio_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

// 0x4BE880. Writes the three settings back to the registry unless the argument
// is set. ECX is read as one byte (`test cl, cl`), so a BYTE.
void exit_video_opts(BYTE bSkipSave);

// 0x4BE8D0. The gamma slider. It ends in a tail jump to scroll_ctrl_scroll and
// returns its int.
int video_dlg_gamma_slider_fcn(ctrl_t *pCtrl, dlgevt_t *pEvent);

#endif  // OC117_VIDEODLG_H
