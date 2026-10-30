// CNetUnkn.h -- the per-block scratch object src117/lang/net_record.cpp drains
// into: a byte run and a parallel size array, with a flag recording whether
// this object allocated them itself. Each offset's assert names the instruction
// that fixes it.

#ifndef OC117_CNETUNKN_H
#define OC117_CNETUNKN_H

#include "platform.h"

class CNetUnkn {
public:
  CNetUnkn(BYTE *pBytesIn, int *pnSizesIn, DWORD nCountIn);
  ~CNetUnkn();

  /*0x00*/ BYTE *pBytes;
  /*0x04*/ int  *pnSizes;
  /*0x08*/ DWORD nCount;
  /*0x0C*/ int   bOwned;
};

static_assert(offsetof(CNetUnkn, pBytes) == 0x00, "SMemFree(dword ptr [esi]) in the destructor");
OC_X86_LAYOUT(offsetof(CNetUnkn, pnSizes) == 0x04, "SMemFree(dword ptr [esi+4h]) in the destructor");
OC_X86_LAYOUT(offsetof(CNetUnkn, nCount) == 0x08, "shl eax, 2h on [esi+8h] in net_record_expand");
OC_X86_LAYOUT(offsetof(CNetUnkn, bOwned) == 0x0C, "cmp dword ptr [esi+0Ch], 0h in the destructor");
OC_X86_LAYOUT(sizeof(CNetUnkn) == 0x10, "three constructor arguments plus the flag, no tail padding");

#endif  // OC117_CNETUNKN_H
