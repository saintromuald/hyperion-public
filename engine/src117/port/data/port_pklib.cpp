// port_pklib.cpp -- PKWARE's implode, explode and CRC-32 for compress.cpp, from
// StormLib's pklib. The original has the PKWARE
// Data Compression Library linked in at 0x401000; pklib is written to the same
// interface and work buffers, and pklib_test checks that the two write the
// same bytes.
//
// pklib's sources are compiled with their names turned into PkImplode,
// PkExplode and PkCrc32 (build_engine_x64.py), so that these can be the game's.
// This file and pklib are compiled with /Gd, not the engine's /Gr: pklib.h
// spells the callbacks' pointer types with no calling convention, and the
// game's are __cdecl.
#include "compress.h"

#define implode PkImplode
#define explode PkExplode
#define crc32_pklib PkCrc32
// pklib's unsigned long is 32 bits wide on Windows; off Windows the build compiles pklib
// with long as int (build_engine_linux.py), so its structures are the same here.
#if !defined(_WIN32)
#define long int
#endif
#include "pklib.h"
#if !defined(_WIN32)
#undef long
#endif
#undef implode
#undef explode
#undef crc32_pklib

// compress and decompress allocate the work buffers at these sizes, the original's
// on x86. pklib's structures hold pointers, so on x64 they grow.
OC_X86_LAYOUT(sizeof(TCmpStruct) == 0x8DD8, "implode's work buffer: SMemAlloc(0x8DD8) in compress");
OC_X86_LAYOUT(sizeof(TDcmpStruct) == 0x3134, "explode's work buffer: SMemAlloc(0x3134) in decompress");
extern const unsigned int g_pkImplodeWorkSize = sizeof(TCmpStruct);
extern const unsigned int g_pkExplodeWorkSize = sizeof(TDcmpStruct);

unsigned int __cdecl implode(PFN_COMP_READ pfnRead, PFN_COMP_WRITE pfnWrite, char *pWork, void *pParam,
                             unsigned int *pnType, unsigned int *pnDictSize)
{
  return PkImplode(pfnRead, pfnWrite, pWork, pParam, pnType, pnDictSize);
}

unsigned int __cdecl explode(PFN_COMP_READ pfnRead, PFN_COMP_WRITE pfnWrite, char *pWork, void *pParam)
{
  return PkExplode(pfnRead, pfnWrite, pWork, pParam);
}

// pklib spells the CRC unsigned long, 32 bits on Windows as unsigned int is.
unsigned int __cdecl crc32_pklib(const void *pBuf, unsigned int *pnSize, unsigned int *pdwCrc)
{
  static_assert(sizeof(ULONG) == sizeof(unsigned int), "the CRC's width");
  return PkCrc32(static_cast<char *>(const_cast<void *>(pBuf)), pnSize, reinterpret_cast<ULONG *>(pdwCrc));
}
