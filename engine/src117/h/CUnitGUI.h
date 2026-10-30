// CUnitGUI.h -- the free functions of src117/lang/CUnitGUI.cpp, the unit GUI
// layer: selection and team circles, damage and shield overlays, the
// screen-visibility queries and the buttonset. The TU's CUnit members are
// declared in CUnit.h. The free functions are __fastcall through /Gr and are
// declared without the keyword.

#ifndef OC117_CUNITGUI_H
#define OC117_CUNITGUI_H

#include "platform.h"

class CUnit;

// 0x423120. EAX at the ret is target_is_on's leftover, and all four callers
// discard it.
void unit_gui_reset();

// 0x422B40. Picks `this` or its subunit on a units.dat flag and tail-jumps into
// CUnit::sub_423000, so the one argument, in ECX, is a CUnit *.
void sub_422B40(CUnit *pUnit);

#endif // OC117_CUNITGUI_H
