// sha1.h -- declarations for src117/lang/sha1.cpp: the SHA-1 core. Free
// functions are __fastcall through /Gr and are declared without the keyword.

#ifndef OC117_SHA1_H
#define OC117_SHA1_H

#include "platform.h"
#include "sha.h"

// All four are ECX/EDX fastcalls; only SHA1_Update takes a stack word (`ret 4h`).
// Every caller discards EAX.
void SHA1_Final(void *pvHash, shactx_t *pCtx);
void SHA1_Init(shactx_t *pCtx);
void SHA1_Transform(DWORD *pdwState, BYTE *pbBuffer);
void SHA1_Update(shactx_t *pCtx, const void *pvData, DWORD cbData);

#endif  // OC117_SHA1_H
