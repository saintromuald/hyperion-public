// gds_appexi.h -- declarations for src117/lang/gds_appexi.cpp, running the
// exit callbacks and leaving the process. Free functions are __fastcall through
// /Gr and are declared without the keyword.

#ifndef OC117_GDS_APPEXI_H
#define OC117_GDS_APPEXI_H

#include "platform.h"

// 0x4CF6B0 (125 B). CL is passed through to every callback; EAX at the ret is
// inherited and app_exit, the one caller, discards it.
void app_run_exit(BYTE bFatal);

// 0x4CF730 (40 B). CL is the fatal flag and becomes the exit code; the body
// tail-jumps gds_exit and never returns.
void app_exit(BYTE bFatal);

#endif  // OC117_GDS_APPEXI_H
