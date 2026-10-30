// header.h -- declarations for src117/lang/header.cpp, the map header read. The
// free functions are __fastcall through /Gr and are declared without the
// keyword.

#ifndef OC117_HEADER_H
#define OC117_HEADER_H

#include "platform.h"

// 0x464560. pData and nSize in ECX and EDX, the seed and its size on the
// stack; maphdr_VCOD's two calls use the full result.
int sub_464560(void *pData, int nSize, void *pSeed, int nSeedSize);

#endif  // OC117_HEADER_H
