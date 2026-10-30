// gds_inifile.cpp -- the preferences table: defaults, loading from and saving
// to the Starcraft registry key. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gds_inifile.h"
#include "../h/Storm.h"
#include "../h/spd_dlg.h"   // SRegLoadValue, SRegSaveValue
#include <string.h>
#include "../gen/globals117.h"

// Globals this TU reaches, dual-spelled.

// sgPrefs -- 0x50F168, sixteen entries; the table ends at 0x50F2E8, where
// _exit_slot begins.
#ifdef OC_DLL_MODE
#define sgPrefs (*reinterpret_cast<PREFENTRY (*)[16]>(OC_VA(0x50F168u)))
#else
extern PREFENTRY sgPrefs[16] OC_UNALIGNED;
#endif

// ini_file_close -- 0x4CE530. Saves every preference unless the exit is fatal.
// In all three bodies the counter is a `while (n--)` beside the pointer and
// the case arms are in 1.17's layout order (2, 1, 0).
void ini_file_close(BYTE bFatal)
{
  if (bFatal)
    return;
  PREFENTRY *pPref = sgPrefs;
  for (int n = 16; n--; ++pPref) {
    switch (pPref->dwType) {
    case 2:
      SRegSaveValue("Starcraft", pPref->pszName, 0,
                    *static_cast<DWORD *>(pPref->pvValue) & pPref->dwMax);
      break;
    case 1:
      SRegSaveValue("Starcraft", pPref->pszName, 0, *static_cast<DWORD *>(pPref->pvValue));
      break;
    case 0:
      SRegSaveString("Starcraft", pPref->pszName, 0, static_cast<char *>(pPref->pvValue));
      break;
    }
  }
}

// ini_file_init -- 0x4CE5A0. Resets the defaults, then loads each preference
// that the registry holds. The counter is set before the set_default_prefs call.
void ini_file_init()
{
  char szValue[260];
  DWORD dwValue;
  int n = 16;
  set_default_prefs();
  for (PREFENTRY *pPref = sgPrefs; n--; ++pPref) {
    switch (pPref->dwType) {
    case 2:
      if (SRegLoadValue("Starcraft", pPref->pszName, 0, &dwValue)) {
        if (dwValue)
          *static_cast<DWORD *>(pPref->pvValue) |= pPref->dwMax;
        else
          *static_cast<DWORD *>(pPref->pvValue) &= ~pPref->dwMax;
      }
      break;
    case 1:
      if (SRegLoadValue("Starcraft", pPref->pszName, 0, &dwValue)) {
        if (dwValue < pPref->dwMin || dwValue > pPref->dwMax)
          dwValue = pPref->dwDefault;
        *static_cast<DWORD *>(pPref->pvValue) = dwValue;
      }
      break;
    case 0:
      if (SRegLoadString("Starcraft", pPref->pszName, 0, szValue, sizeof(szValue))) {
        if (strlen(szValue) < pPref->dwDefault)
          strcpy(static_cast<char *>(pPref->pvValue), szValue);
      }
      break;
    }
  }
}

// set_default_prefs -- 0x4CE6A0. Stores every preference's default.
void set_default_prefs()
{
  PREFENTRY *pPref = sgPrefs;
  for (int n = 16; n--; ++pPref) {
    switch (pPref->dwType) {
    case 2:
      if (pPref->dwDefault)
        *static_cast<DWORD *>(pPref->pvValue) |= pPref->dwMax;
      else
        *static_cast<DWORD *>(pPref->pvValue) &= ~pPref->dwMax;
      break;
    case 1:
      *static_cast<DWORD *>(pPref->pvValue) = pPref->dwDefault;
      break;
    case 0:
      *static_cast<char *>(pPref->pvValue) = 0;
      break;
    }
  }
}
