// idea.h -- declarations for src117/lang/idea.cpp: the IDEA block cipher the
// CD-key path encrypts with.
//
// Class members belong in the class header; this file owns the free functions,
// and every free function the .cpp defines is declared here.
//
// Free functions are spelled without an explicit __fastcall: /Gr already makes
// it the default.

#ifndef OC117_IDEA_H
#define OC117_IDEA_H

#include "platform.h"

// The expanded key is 52 WORDs: ideaExpandKey copies the 8 key words across and
// then writes one more word per turn of a 0x2Ch-iteration loop, and IdeaDestroy
// clears three contexts of 0x68h bytes (0x68h == 52 * 2).
#define IDEA_KEYLEN 52

// Eight rounds plus the output transform: ideaCipher's round counter starts at
// 8 and ideaInvertKey's middle loop runs seven times, one short of it.
#define IDEA_ROUNDS 8

// 0x464FB0 (479 B). ECX = the 8-byte input block, EDX = the 8-byte output, one
// stack word = the expanded key. Nothing consumes EAX at either call site.
void ideaCipher(const WORD *in, WORD *out, const WORD *key);

// 0x465190 (300 B). ECX is the expanded encryption key and EDX the 52-word
// destination the decryption key is built into; both are walked as WORDs.
void ideaInvertKey(const WORD *EK, WORD *DK);

// 0x4652C0 (123 B). ECX is the eight key words and EDX the 52-word expansion.
void ideaExpandKey(const WORD *pKey, WORD *pExpanded);

// 0x465340 (21 B).
void IdeaDestroy();

// 0x465360 (30 B).
void IdeaEncrypt(int nCtx, intptr_t nIn, intptr_t nOut);

// 0x465380 (113 B). ECX selects one of the three contexts, EDX is nonzero to
// install an encryption key and zero to install its inverse, and the stack word
// points at the 16 key bytes.
void IdeaInitialize(int nCtx, int bEncrypt, void *pKey);

// 0x465400 (103 B). ECX only, read as 32 bits. The return is a full int even
// though all six callers use only the low word: two of the four return paths
// mask with `and eax, 0FFFFh` and two leave EAX alone, which only holds if the
// mask is in the source.
unsigned int mulInv(unsigned int nArg);

#endif  // OC117_IDEA_H
