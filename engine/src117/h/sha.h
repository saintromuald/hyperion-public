// sha.h -- the SHA-1 hash wrapper in src117/lang/sha.cpp (0x49CFF0 to
// 0x49D370): four thin entry points around three shared contexts, and the
// block-update core.

#ifndef OC117_SHA_H
#define OC117_SHA_H

#include "platform.h"

// One hash context, 0x5C bytes: the five state words, the 64-bit bit count and
// the 64-byte block. signature.cpp builds one on the stack for SHA1_Init.
struct shactx_t {
  /*0x00*/ DWORD dwState[5];
  /*0x14*/ DWORD dwBitsLo;
  /*0x18*/ DWORD dwBitsHi;
  /*0x1C*/ BYTE bBlock[64];
};
static_assert(offsetof(shactx_t, dwState) == 0x00, "ShaInitialize mov [eax], 67452301h");
static_assert(offsetof(shactx_t, dwBitsLo) == 0x14, "ShaUpdateHash mov eax, [ebx+14h]");
static_assert(offsetof(shactx_t, dwBitsHi) == 0x18, "ShaUpdateHash inc dword ptr [ebx+18h]");
static_assert(offsetof(shactx_t, bBlock) == 0x1C, "ShaUpdateHash movdqu [ebx+1Ch], xmm0");
static_assert(sizeof(shactx_t) == 0x5C, "imul ecx, ecx, 5Ch");

void ShaDestroy();

// Copies the five state words out as dwords.
void ShaGetLastHash(int nCtx, DWORD *pdwHash);

// Hashes one 0x40-byte block: EDX goes to ShaUpdateHash untouched.
void ShaHash(int nCtx, const void *pvBlock, DWORD *pdwHash);

void ShaInitialize(int nCtx);

// ECX is the context; ShaHash forms it as currinfo + nCtx * 0x5C.
void ShaUpdateHash(shactx_t *pCtx, const void *pvData, int cbData);

#endif  // OC117_SHA_H
