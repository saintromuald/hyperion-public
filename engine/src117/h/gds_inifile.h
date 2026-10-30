// gds_inifile.h -- declarations for src117/lang/gds_inifile.cpp, the
// preferences table kept under the Starcraft registry key. The free functions are
// __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_GDS_INIFILE_H
#define OC117_GDS_INIFILE_H

#include "platform.h"

// One preference. dwType 0 is a string of at most dwDefault bytes, 1 a value
// clamped to dwMin..dwMax, 2 a flag whose bit mask sits in dwMax.
struct PREFENTRY {
  /*0x00*/ DWORD dwType;
  /*0x04*/ const char *pszName;
  /*0x08*/ DWORD dwMin;
  /*0x0C*/ DWORD dwMax;       // the upper bound, or the mask for a flag
  /*0x10*/ DWORD dwDefault;   // the default, or the buffer size for a string
  /*0x14*/ void *pvValue;
};
static_assert(offsetof(PREFENTRY, dwType) == 0x00, "PREFENTRY::dwType");
OC_X86_LAYOUT(offsetof(PREFENTRY, pszName) == 0x04, "PREFENTRY::pszName");
OC_X86_LAYOUT(offsetof(PREFENTRY, dwMin) == 0x08, "PREFENTRY::dwMin");
OC_X86_LAYOUT(offsetof(PREFENTRY, dwMax) == 0x0C, "PREFENTRY::dwMax");
OC_X86_LAYOUT(offsetof(PREFENTRY, dwDefault) == 0x10, "PREFENTRY::dwDefault");
OC_X86_LAYOUT(offsetof(PREFENTRY, pvValue) == 0x14, "PREFENTRY::pvValue");
OC_X86_LAYOUT(sizeof(PREFENTRY) == 0x18, "PREFENTRY stride");

// 0x4CE530. CL is the fatal flag. No direct caller: init_program registers it
// with app_add_exit, and app_run_exit passes it in CL.
void ini_file_close(BYTE bFatal);

// 0x4CE5A0. Nullary; EAX at the ret is inherited and init_program discards it.
void ini_file_init();

// 0x4CE6A0. Nullary; ini_file_init, the one caller, discards EAX.
void set_default_prefs();

#endif  // OC117_GDS_INIFILE_H
