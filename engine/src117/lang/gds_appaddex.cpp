// gds_appaddex.cpp -- registering an exit callback. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>
#include "../h/gds_appaddex.h"
#include "../h/gds_appcrit.h"
#include "../h/Storm.h"
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// _app_exits -- 0x6D3178 (gds/appexi.cpp's .data). A pointer to 32 callbacks,
// allocated on first use.
#ifdef OC_DLL_MODE
#define _app_exits (*reinterpret_cast<APPEXITPROC **>(OC_VA(0x6D3178u)))
#else
extern APPEXITPROC *_app_exits;
#endif

// app_add_exit -- 0x4CE940. Registers a callback once. A callback already in
// the table returns FALSE; otherwise it takes the first empty slot and a full
// table is fatal.
BOOL __fastcall app_add_exit(APPEXITPROC pfnExit)
{
  if (!_app_exits) {
    _app_exits = static_cast<APPEXITPROC *>(SMemAlloc(32 * sizeof(APPEXITPROC), "appaddex.cpp", 42, 0));
    memset(_app_exits, 0, 32 * sizeof(APPEXITPROC));
  }
  int nFree = -1;
  for (int i = 0; i < 32; ++i) {
    if (_app_exits[i] == pfnExit)
      return FALSE;
    if (nFree == -1 && !_app_exits[i])
      nFree = i;
  }
  if (nFree == -1)
    appcritf("APPADDEX:1");
  _app_exits[nFree] = pfnExit;
  return TRUE;
}
