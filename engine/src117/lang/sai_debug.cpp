// sai_debug.cpp -- the AI pathing debug hook: the exit callback the path module
// registers at startup.

#include "../h/sai.h"        // BOOL sai_debug_init()
#include "../h/dlg_dlg.h"    // app_add_exit
#include "../h/net_cmds.h"   // voice_squelch, the one-byte ret at 0x4BE950
#include "../gen/globals117.h"

// 0x48C6C0 (10 B). Registers 0x4BE950 as an exit callback. 1.17 tail-jumps
// app_add_exit and returns what it left in EAX; BOOL because sai_path_init
// returns this, and TRUE because falling off the end is undefined in C++. The
// cast is init.cpp's idiom for an app_add_exit argument of another arity.
BOOL sai_debug_init()
{
  app_add_exit(reinterpret_cast<void (__fastcall *)(void)>(voice_squelch));
  return TRUE;
}
