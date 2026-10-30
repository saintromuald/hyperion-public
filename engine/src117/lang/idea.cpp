// idea.cpp -- the IDEA block cipher. Bodies are in oracle VA order.
//
// A callee defined in this file can be spliced into a caller in this file by
// /Ob2, which the oracle (WPO off) never does across TUs; the lever against it
// is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/idea.h"
#include "../gen/globals117.h"

#include <string.h>
#include <windows.h>   // SecureZeroMemory

// Globals this TU reaches, dual-spelled, with one #ifdef per name rather than
// one over the block: a single guard would undeclare every name under it.

// 0x66CB70 g_cryptInfo -- the sheet's name (idea.cpp). Three expanded keys of
// 0x68h bytes: IdeaDestroy memsets 0x138h here and 0x138h == 3 * 0x68h, and
// IdeaEncrypt indexes with `imul eax, ecx, 68h`.
#ifndef OC_G_CRYPTINFO
#define OC_G_CRYPTINFO
#ifdef OC_DLL_MODE
#define g_cryptInfo (reinterpret_cast<WORD *>(OC_VA(0x66CB70u)))
#else
extern WORD g_cryptInfo[3 * IDEA_KEYLEN];
#endif
#endif

// The arithmetic modulo 0x10001 the cipher is built on. Both operands are held
// in full registers and narrowed only where the algorithm needs it, so the
// masks below are part of the shape and not redundant.

#define idea_low16(x)  ((x) & 0xFFFFu)
#define idea_high16(x) ((x) >> 16)

// Multiplication with 0 standing for 0x10000. t16 and tx are both assigned
// before either test -- the truncation of x goes to tx and x itself is written
// only on the three exits, which is what `movzx edx, si` against a live ESI
// proves.
#define IDEA_MUL(x, y)                              \
  (t16 = (y), tx = idea_low16(x),                   \
   tx ? (t16 ? (t32 = tx * t16,                     \
                tx = idea_low16(t32),               \
                t16 = idea_high16(t32),             \
                x = tx - t16 + (tx < t16))          \
             : (x = 1 - tx))                        \
      : (x = 1 - t16))

// The bodies, in oracle VA order.

// 0x464FB0. Eight rounds plus the output transform over one 8-byte block. The
// previous x2 and x3 stay readable in their stack homes, which is what the s2
// and s3 of the round tail read.
void ideaCipher(const WORD *in, WORD *out, const WORD *key)
{
  unsigned int x1, x2, x3, x4, u2, u3;
  int r = IDEA_ROUNDS;
  unsigned int t16;
  unsigned int tx;
  DWORD t32;

  x1 = *in++;
  x2 = *in++;
  x3 = *in++;
  x4 = *in;

  do {
    IDEA_MUL(x1, *key++);
    x2 += *key++;
    x3 += *key++;
    IDEA_MUL(x4, *key++);

    u3 = x3 ^ x1;
    IDEA_MUL(u3, *key++);
    u2 = x2 ^ x4;
    u2 += u3;
    // The sixth key word is read in place and the pointer bumped at the bottom,
    // so the multiply re-reads it from [key+0Ah] instead of spilling it.
    IDEA_MUL(u2, *key);
    u3 += u2;

    x1 ^= u2;
    x4 ^= u3;
    u2 ^= x3;
    u3 ^= x2;
    x2 = u2;
    x3 = u3;
    key++;
  } while (--r);

  // Each output word is stored as it is formed, walking the out parameter itself.
  // A separate pointer copied from it is reloaded from its slot instead.
  IDEA_MUL(x1, *key++);
  *out++ = x1;
  *out++ = x3 + *key++;
  *out++ = x2 + *key++;
  IDEA_MUL(x4, *key);
  *out = x4;
}

// 0x465190. Builds the decryption key from the expanded encryption key, back to
// front into a local, then copies it out and wipes the local in one pass.
void ideaInvertKey(const WORD *EK, WORD *DK)
{
  WORD temp[IDEA_KEYLEN];
  WORD *p = temp + IDEA_KEYLEN;
  unsigned int t1, t2, t3;
  int i;

  t1 = mulInv(*EK++);
  t2 = -*EK++;
  t3 = -*EK++;
  *--p = static_cast<WORD>(mulInv(*EK++));
  *--p = static_cast<WORD>(t3);
  *--p = static_cast<WORD>(t2);
  *--p = static_cast<WORD>(t1);

  for (i = 0; i < IDEA_ROUNDS - 1; i++) {
    t1 = *EK++;
    *--p = *EK++;
    *--p = static_cast<WORD>(t1);

    t1 = mulInv(*EK++);
    t2 = -*EK++;
    t3 = -*EK++;
    *--p = static_cast<WORD>(mulInv(*EK++));
    *--p = static_cast<WORD>(t2);
    *--p = static_cast<WORD>(t3);
    *--p = static_cast<WORD>(t1);
  }

  t1 = *EK++;
  *--p = *EK++;
  *--p = static_cast<WORD>(t1);

  t1 = mulInv(*EK++);
  t2 = -*EK++;
  t3 = -*EK++;
  *--p = static_cast<WORD>(mulInv(*EK++));
  *--p = static_cast<WORD>(t3);
  *--p = static_cast<WORD>(t2);
  *--p = static_cast<WORD>(t1);

  for (i = 0, p = temp; i < IDEA_KEYLEN; i++) {
    t1 = *p;
    *DK++ = static_cast<WORD>(t1);
    *p++ = 0;
  }
}

// 0x4652C0. Expands the eight key words to 52 by rotating the last eight left
// by 25 bits, one word at a time.
void ideaExpandKey(const WORD *pKey, WORD *pExpanded)
{
  const WORD *userkey = pKey;
  WORD *EK = pExpanded;
  int i, j;

  for (j = 0; j < 8; j++)
    EK[j] = userkey[j];

  for (i = 0; j < IDEA_KEYLEN; j++) {
    i++;
    EK[i + 7] = static_cast<WORD>((EK[i & 7] << 9) | (EK[(i + 1) & 7] >> 7));
    EK += i & 8;
    i &= 7;
  }
}

// 0x465340. Wipes all three expanded keys.
void IdeaDestroy()
{
  memset(g_cryptInfo, 0, 3 * IDEA_KEYLEN * sizeof(WORD));
}

// 0x465360. Runs one block through the context nCtx names.
void IdeaEncrypt(int nCtx, intptr_t nIn, intptr_t nOut)
{
  ideaCipher(reinterpret_cast<const WORD *>(nIn), reinterpret_cast<WORD *>(nOut),
             g_cryptInfo + nCtx * IDEA_KEYLEN);
}

// 0x465380. Installs an encryption or decryption key into context nCtx. Only
// the decrypt arm wipes temp, which is what its `jmp` past that loop proves.
// SecureZeroMemory is the oracle's byte loop; memset would be a `rep stos`.
void IdeaInitialize(int nCtx, int bEncrypt, void *pKey)
{
  WORD key[8];
  WORD temp[IDEA_KEYLEN];

  memcpy(key, pKey, sizeof(key));
  if (bEncrypt) {
    ideaExpandKey(key, g_cryptInfo + nCtx * IDEA_KEYLEN);
  } else {
    ideaExpandKey(key, temp);
    ideaInvertKey(temp, g_cryptInfo + nCtx * IDEA_KEYLEN);
    SecureZeroMemory(temp, sizeof(temp));
  }
  SecureZeroMemory(key, sizeof(key));
}

// 0x465400. The multiplicative inverse of nArg modulo 0x10001, by the extended
// Euclidean algorithm. Zero and one are their own inverses.
unsigned int mulInv(unsigned int nArg)
{
  unsigned int x = nArg;
  if (x <= 1)
    return x;

  unsigned int t1 = 0x10001u / x;
  unsigned int y = 0x10001u % x;
  if (y == 1)
    return (y - t1) & 0xFFFFu;

  unsigned int t0 = 1;
  do {
    unsigned int q = x / y;
    x = x % y;
    t0 += q * t1;
    if (x == 1)
      return t0;
    q = y / x;
    y = y % x;
    t1 += q * t0;
  } while (y != 1);
  return (y - t1) & 0xFFFFu;
}
