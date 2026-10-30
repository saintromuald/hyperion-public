// sha.cpp -- the SHA-1 hash wrapper, oracle 0x49CFF0-0x49D370: three shared
// contexts in .data, the four entry points over them, and the block transform.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/sha.h"
#include "../gen/globals117.h"

// 0x6A60A8: the three contexts the entry points index. The extent is
// ShaDestroy's `push 114h`, 3 * 0x5C. Spelled as an array reference rather than
// a pointer so sizeof survives the shipped form.


#ifdef OC_DLL_MODE
  #ifndef currinfo
  #define currinfo (*reinterpret_cast<shactx_t (*)[3]>(OC_VA(0x6A60A8u)))
  #endif
#else
extern shactx_t currinfo[3] OC_UNALIGNED;
#endif

// The bodies, in oracle VA order.

// Clear all three contexts.
void ShaDestroy() {
  memset(currinfo, 0, sizeof(currinfo));
}

// Publish one context's five state words, if the caller wants them. A loop
// rather than five assignments, so ShaHash recomputes nCtx * 0x5C after the call
// as 1.17 does.
void ShaGetLastHash(int nCtx, DWORD *pdwHash) {
  if (pdwHash != 0) {
    for (int i = 0; i < 5; i++)
      pdwHash[i] = currinfo[nCtx].dwState[i];
  }
}

// Fold one 64-byte block into a context and read the state back out.
void ShaHash(int nCtx, const void *pvBlock, DWORD *pdwHash) {
  ShaUpdateHash(&currinfo[nCtx], pvBlock, 0x40);
  ShaGetLastHash(nCtx, pdwHash);
}

// Seed a context with the SHA-1 initial state and a zero bit count.
void ShaInitialize(int nCtx) {
  shactx_t *pCtx = &currinfo[nCtx];
  pCtx->dwState[0] = 0x67452301;
  pCtx->dwState[1] = 0xEFCDAB89;
  pCtx->dwState[2] = 0x98BADCFE;
  pCtx->dwState[3] = 0x10325476;
  pCtx->dwState[4] = 0xC3D2E1F0;
  pCtx->dwBitsLo = 0;
  pCtx->dwBitsHi = 0;
}

// Fold whole 64-byte blocks into a context. The rotates are written with signed
// shifts because 1.17 uses `sar`, and the message schedule has no rotate at all,
// so this is SHA-0 rather than SHA-1; both are kept, not corrected.
void ShaUpdateHash(shactx_t *pCtx, const void *pvData, int cbData) {
  int adwW[80];
  const BYTE *pbData;
  DWORD dwOldLo;
  DWORD dwNewLo;
  DWORD nBlocks;
  int a, b, c, d, e, t;
  int i;

  pbData = static_cast<const BYTE *>(pvData);
  dwOldLo = pCtx->dwBitsLo;
  dwNewLo = dwOldLo + cbData * 8;
  if (static_cast<LONG>(dwNewLo) < static_cast<LONG>(dwOldLo))
    pCtx->dwBitsHi++;
  pCtx->dwBitsLo = dwNewLo;
  pCtx->dwBitsHi += cbData >> 29;
  if (cbData < 64)
    return;

  nBlocks = static_cast<DWORD>(cbData) >> 6;
  do {
    memcpy(pCtx->bBlock, pbData, 64);
    for (i = 0; i < 16; i++)
      adwW[i] = reinterpret_cast<const DWORD *>(pCtx->bBlock)[i];
    for (i = 16; i < 80; i++)
      adwW[i] = adwW[i - 3] ^ adwW[i - 8] ^ adwW[i - 16] ^ adwW[i - 14];

    a = pCtx->dwState[0];
    b = pCtx->dwState[1];
    c = pCtx->dwState[2];
    d = pCtx->dwState[3];
    e = pCtx->dwState[4];
    for (i = 0; i < 20; i++) {
      t = ((a >> 27) | (a << 5)) + ((b & c) | (~b & d)) + e + 0x5A827999 +
          adwW[i];
      e = d;
      d = c;
      c = (b >> 2) | (b << 30);
      b = a;
      a = t;
    }
    for (i = 20; i < 40; i++) {
      t = ((a >> 27) | (a << 5)) + (b ^ c ^ d) + e + 0x6ED9EBA1 + adwW[i];
      e = d;
      d = c;
      c = (b >> 2) | (b << 30);
      b = a;
      a = t;
    }
    for (i = 40; i < 60; i++) {
      t = ((a >> 27) | (a << 5)) + ((b & c) | (b & d) | (c & d)) + e +
          0x8F1BBCDC + adwW[i];
      e = d;
      d = c;
      c = (b >> 2) | (b << 30);
      b = a;
      a = t;
    }
    for (i = 60; i < 80; i++) {
      t = ((a >> 27) | (a << 5)) + (b ^ c ^ d) + e + 0xCA62C1D6 + adwW[i];
      e = d;
      d = c;
      c = (b >> 2) | (b << 30);
      b = a;
      a = t;
    }
    pCtx->dwState[0] += a;
    pCtx->dwState[1] += b;
    pCtx->dwState[2] += c;
    pCtx->dwState[3] += d;
    pCtx->dwState[4] += e;
    pbData += 64;
  } while (--nBlocks != 0);
}
