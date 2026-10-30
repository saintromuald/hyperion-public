// codec.h -- declarations for src117/lang/codec.cpp, the file decoder built on
// IDEA and SHA.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_CODEC_H
#define OC117_CODEC_H

#include "platform.h"

// 0x4119F0 (588 B). ECX the data, EDX its length, the key string as the one
// stack word (`ret 4h`). Returns the decoded length, 0 on failure.
int DecodeFile(void *pvData, DWORD dwSize, intptr_t nKey);

// 0x411C40 (510 B). ECX the IDEA direction flag, EDX the key string; DecodeFile
// discards EAX.
void InitializeKeys(int bEncrypt, const char *pszKey);

#endif  // OC117_CODEC_H
