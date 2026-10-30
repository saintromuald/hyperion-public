// statselect.cpp -- the status console's reaction to a selection change. Bodies
// are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/statselect.h"
#include "../h/CUnit.h"
#include "../h/select.h"          // select_units
#include "../h/statcmd.h"         // request_statcmd_update
#include "../h/statdata.h"        // request_statdata_update
#include "../h/statport.h"        // request_statport_update
#include "../gen/globals117.h"
#include "../h/selectlimits.h"  // Hyperion: SELECTION_MAX

// Globals this TU reaches, dual-spelled.

// The dirty flag status_update drains.
#ifdef OC_DLL_MODE
#define sgbStatusChange (*reinterpret_cast<BYTE *>(OC_VA(0x6AB4FCu)))
#else
extern BYTE sgbStatusChange;
#endif

// Marks the console dirty and asks its three panels to redraw.
void status_change_selection() {
  sgbStatusChange = 1;
  request_statcmd_update();
  request_statport_update();
  request_statdata_update();
}

// Reselects the staged new units without this one.
void CUnit::sub_4A7B80() {
  CUnit *apUnits[SELECTION_WIDE];
  int nCount = 0;
  for (int i = 0; i < SelectionMax(); i++) {
    CUnit *pUnit = NewUnits()[i];
    if (!pUnit)
      break;
    if (this != pUnit)
      apUnits[nCount++] = pUnit;
  }
  select_units(nCount, apUnits, 0, 1);
}
