// EnumModuleData.h -- the fixed collector ExecuteEnumCallback appends to, one
// MODULE_DEBUG_INFO per loaded module.

#ifndef OC117_ENUMMODULEDATA_H
#define OC117_ENUMMODULEDATA_H

#include "platform.h"
#include "Module.h"

// nCount entries of aItems are filled; ExecuteEnumCallback stops at 0x100.
struct EnumModuleData {
  /*0x000*/ DWORD nCount;
  /*0x004*/ MODULE_DEBUG_INFO aItems[0x100];

  EnumModuleData();
};

static_assert(offsetof(EnumModuleData, nCount) == 0x000, "0x405010: imul eax, dword ptr [ebx], 228h");
OC_X86_LAYOUT(offsetof(EnumModuleData, aItems) == 0x004, "0x403340: lea edx, [ecx+4h]");
OC_X86_LAYOUT(sizeof(EnumModuleData) == 0x004 + 0x100 * 0x228, "0x405010: cmp dword ptr [ebx], 100h");

#endif  // OC117_ENUMMODULEDATA_H
