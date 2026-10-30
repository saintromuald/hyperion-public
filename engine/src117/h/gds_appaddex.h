// gds_appaddex.h -- declarations for src117/lang/gds_appaddex.cpp, which
// registers the exit callbacks gds_appexi.cpp runs.

#ifndef OC117_GDS_APPADDEX_H
#define OC117_GDS_APPADDEX_H

#include "platform.h"

// An exit callback takes nothing and returns nothing: RunGame, cursor_init and
// the rest hand app_add_exit a nullary __fastcall address.
typedef void (__fastcall *APPEXITPROC)(void);

// app_add_exit -- 0x4CE940. ECX is the callback. Returns 1 on the insert and 0
// on a duplicate; all 25 callers discard it.
BOOL __fastcall app_add_exit(APPEXITPROC pfnExit);

#endif  // OC117_GDS_APPADDEX_H
