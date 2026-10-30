// CUnitRClick.h -- declarations for src117/lang/CUnitRClick.cpp: the right-click
// order dispatch, which turns a queued right click into an issued order. Free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_CUNITRCLICK_H
#define OC117_CUNITRCLICK_H

#include "platform.h"

class CUnit;

// 0x47F530 (327 B). ECX and EDX live and two stack words (`ret 8h`). Its one
// caller, CUnit::DispatchRClick, passes the unit, wOrderTargetX, wOrderTargetY
// and the WORD at CUnit+0x50, which this body forwards to GetRClickTarget's
// `int nType`. The caller discards EAX, so void.
void sub_47F530(CUnit *pUnit, int nX, int nY, int nUnitType);

#endif  // OC117_CUNITRCLICK_H
