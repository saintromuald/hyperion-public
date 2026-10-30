// gluGameMode.h -- declarations for src117/lang/gluGameMode.cpp, the cinematic
// flic chooser: one body that maps a campaign id to its intro and outro flics
// through a .rdata pair table. Free functions are __fastcall through /Gr and are
// declared without the keyword.

#ifndef OC117_GLUGAMEMODE_H
#define OC117_GLUGAMEMODE_H

#include "platform.h"
#include "dialog.h"
#include "playflic.h"   // play_flic

// 0x458FC0 (60 B). ECX only, read as 32 bits; no caller reads EAX.
void play_cinematic_flic(int nArg0);



#endif  // OC117_GLUGAMEMODE_H
