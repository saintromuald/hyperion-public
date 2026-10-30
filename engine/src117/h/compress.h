// compress.h -- declarations for src117/lang/compress.cpp: PKWARE implode/explode
// wrappers.
//
// Free functions are __fastcall by /Gr's default, so their declarations do not
// spell it. Three names compress.cpp defines are declared elsewhere and are not
// repeated here: `compress` and `decompress` in saveload.h, `comp_crc32` in
// fd_user.h; the definitions copy those lines verbatim.

#ifndef OC117_COMPRESS_H
#define OC117_COMPRESS_H

#include "platform.h"

// The state block the two PKWARE callbacks share with their caller, and the
// only thing the `param` argument of implode/explode ever points at. Every
// field is a dword; the extent is SMemAlloc(0x18) in compress and decompress.
struct comp_info_t {
  /*0x00*/ const char *pSrc;   // read_buf `mov ecx, [edi]`, added to nSrcPos
  /*0x04*/ unsigned nSrcPos;   // read_buf `add dword ptr [edi+4h], esi`
  /*0x08*/ char *pDst;         // write_buf `mov eax, [esi+8h]`, added to nDstPos
  /*0x0C*/ unsigned nDstPos;   // write_buf `add dword ptr [esi+0Ch], eax`
  /*0x10*/ unsigned nSrcSize;  // read_buf `mov esi, [edi+10h]` minus nSrcPos
  /*0x14*/ unsigned nDstSize;  // write_buf `cmp eax, [esi+14h]`
};
static_assert(offsetof(comp_info_t, pSrc) == 0x00, "comp_info_t::pSrc");
OC_X86_LAYOUT(offsetof(comp_info_t, nSrcPos) == 0x04, "comp_info_t::nSrcPos");
OC_X86_LAYOUT(offsetof(comp_info_t, pDst) == 0x08, "comp_info_t::pDst");
OC_X86_LAYOUT(offsetof(comp_info_t, nDstPos) == 0x0C, "comp_info_t::nDstPos");
OC_X86_LAYOUT(offsetof(comp_info_t, nSrcSize) == 0x10, "comp_info_t::nSrcSize");
OC_X86_LAYOUT(offsetof(comp_info_t, nDstSize) == 0x14, "comp_info_t::nDstSize");
OC_X86_LAYOUT(sizeof(comp_info_t) == 0x18, "comp_info_t: SMemAlloc(0x18) in compress");

// The PKWARE callback pair. Both are __cdecl -- a plain `ret` with three stack
// slots read and the library cleaning up -- and their addresses are pushed as
// literals into implode/explode, so the signatures have to be the library's.
typedef unsigned int(__cdecl *PFN_COMP_READ)(char *pBuf, unsigned int *pnSize,
                                             void *pParam);
typedef void(__cdecl *PFN_COMP_WRITE)(char *pBuf, unsigned int *pnSize,
                                      void *pParam);

// The PKWARE library bodies at the foot of .text.

// 0x401000 implode (412 B) / 0x401AE0 explode (373 B). Both __cdecl: the
// wrappers `add esp, 18h` and `add esp, 10h` after the call, for six and four
// arguments in the library's own order.
unsigned int __cdecl implode(PFN_COMP_READ pfnRead, PFN_COMP_WRITE pfnWrite,
                             char *pWork, void *pParam, unsigned int *pnType,
                             unsigned int *pnDictSize);
unsigned int __cdecl explode(PFN_COMP_READ pfnRead, PFN_COMP_WRITE pfnWrite,
                             char *pWork, void *pParam);

// 0x402160 crc32_pklib (60 B), __cdecl with three pushed arguments. The second
// and third are read through once (`mov esi, [ecx]` / `mov eax, [eax]`) as the
// byte count and the running CRC, and EAX at the ret is the new CRC.
unsigned int __cdecl crc32_pklib(const void *pBuf, unsigned int *pnSize,
                                 unsigned int *pdwCrc);

// The TU's own free functions.

// 0x413750 read_buf (51 B) / 0x413790 write_buf (60 B). The callbacks above;
// read_buf returns the byte count it moved, write_buf returns nothing.
unsigned int __cdecl read_buf(char *pBuf, unsigned int *pnSize, void *pParam);
void __cdecl write_buf(char *pBuf, unsigned int *pnSize, void *pParam);

// 0x413940 comp_implode (149 B) / 0x413A40 comp_explode (112 B). Both
// __fastcall with one stack word (`ret 4h`): ECX is the buffer, EDX its length
// and [ebp+8h] an out pointer that takes either gpDestBuffer or, when implode
// did not shrink the block, the source itself. EAX is the resulting length and
// both callers use the full 32 bits.
int comp_implode(const void *pSrc, unsigned int nLen, void **ppDst);
int comp_explode(const void *pSrc, unsigned int nLen, void **ppDst);

// 0x4139E0 comp_free (91 B). Frees the three allocations and nulls them. No
// register input, plain `ret`, nothing consumes EAX.
void comp_free();

#endif  // OC117_COMPRESS_H
