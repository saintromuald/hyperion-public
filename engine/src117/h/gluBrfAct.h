// gluBrfAct.h -- declarations for src117/lang/gluBrfAct.cpp, the mission
// briefing's action dispatcher: one handler per briefing trigger action, all
// reached from brfact_RunAction. Free functions are __fastcall through /Gr and
// are declared without the keyword.

#ifndef OC117_GLUBRFACT_H
#define OC117_GLUBRFACT_H

#include "platform.h"
#include "dialog.h"
#include "TrigAct.h"
// gluRdy.h declares the three glurdy_* portrait helpers these handlers forward
// to, and this TU's brfact_RunAction and brfact_clear_waiting, which gluRdy.cpp
// calls.
#include "gluRdy.h"

// The handlers. brfact_RunAction (0x44FEE0) finds the 32-byte record at
// `esi + (index + 10) * 32`, reads its opcode at +0x1A and jumps through the
// table at 0x503908 with the record still in ECX, so every handler takes a
// TrigAction (TrigAct.h). Each sets an explicit value in EAX that no caller
// reads, which a void body would not emit, so BOOL.

// 0x44FCD0 (54 B).
BOOL brfact_wait(TrigAction *pAct);

// 0x44FD10 (137 B).
BOOL brfact_wav(TrigAction *pAct);

// 0x44FDA0 (29 B).
BOOL brfact_message(TrigAction *pAct);

// 0x44FDC0 (26 B).
BOOL brfact_objectives(TrigAction *pAct);

// 0x44FDE0 (18 B).
BOOL brfact_show(TrigAction *pAct);

// 0x44FE00 (14 B).
BOOL brfact_hide(TrigAction *pAct);

// 0x44FE10 (17 B).
BOOL brfact_talking(TrigAction *pAct);

// 0x44FE30 (135 B).
BOOL brfact_transmission(TrigAction *pAct);

// 0x44FEC0 (30 B). Nullary.
BOOL brfact_tutorial();

#endif  // OC117_GLUBRFACT_H
