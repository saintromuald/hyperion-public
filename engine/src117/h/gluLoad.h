// gluLoad.h -- declarations for src117/lang/gluLoad.cpp, the load-game screen:
// the saved-game list box, its selection and activation handlers, and the
// screen's dialog handler
//
// Free functions are spelled without an explicit __fastcall, which /Gr already
// makes the default. Where a parameter is spelled `int` or `void *`, only its
// 4-byte footprint is derived; a call site that names one should.

#ifndef OC117_GLULOAD_H
#define OC117_GLULOAD_H

#include "platform.h"
#include "dialog.h"
#include "TSList.h"

struct _SAVEGAME;

// The saved-game list, with the members the game's own code reaches.
#pragma pack(push, 1)
template <> struct TSList<_SAVEGAME,TSGetLink<_SAVEGAME> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_SAVEGAME> link;
  // 0x459910 (84 B). Unlinks and frees every saved-game record.
  void Clear(void);
  // Both inlined into sub_459BC0; defined in gluLoad.cpp.
  void LinkNode(_SAVEGAME *pNode, ULONG dwWhere, _SAVEGAME *pBefore);
  _SAVEGAME *NewNode(ULONG dwWhere, int nExtra, DWORD dwFlags);
};
#pragma pack(pop)

// 0x459970 do_load_dlg (103 B). Nullary; it tail-jumps glues_clear_all.
void do_load_dlg();

// 0x4599E0 sub_4599E0 (144 B). Nullary, and no caller reads the EAX it
// inherits from do_pok_cancel_dlg.
void sub_4599E0();

// 0x459A70 gluload_dlg_fcn (121 B). The screen's dialog procedure: two register
// arguments, and an int return. do_load_dlg takes its address into
// dlg_run_modal's PFN_CTRL_EVT slot, which is where the return comes from; the
// two tail jmps to ctrl_dlg, itself an int, agree. One exit sets EAX to 1 and
// the other widens glues_transition_out's BYTE result, so a void definition of
// this cannot be written at all.
int gluload_dlg_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x459AF0 gluLoad_listbox_ctrl_select (193 B). One register argument, and no
// caller reads the EAX it inherits from SStrCopy.
void gluLoad_listbox_ctrl_select(intptr_t nArg0);

// 0x459BC0 sub_459BC0 (375 B). The list box's fill callback, address-taken
// twice: one stack pointer argument and no register input, which is __stdcall
// -- under /Gr that is the same bytes as a fastcall ignoring both registers,
// and only a call site tells the two apart.
void __stdcall sub_459BC0(void *pArg0);

// 0x459D40 gluLoad_listbox_ctrl_fcn (109 B). The list box's event handler: two
// register arguments, and every path to the ret sets EAX, with no caller to
// read it.
BOOL gluLoad_listbox_ctrl_fcn(intptr_t nArg0, intptr_t nArg1);

// 0x459DD0 gluLoad_listbox_ctrl_activate (106 B). One register argument, and it
// tail-jumps gluLoad_listbox_ctrl_select.
void gluLoad_listbox_ctrl_activate(intptr_t nArg0);

#endif  // OC117_GLULOAD_H
