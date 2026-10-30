// sha1.cpp -- the SHA-1 core, oracle 0x4BEAA0-0x4BFF52: final, init, block
// transform and update over the 0x5C-byte shactx_t. Bodies are in oracle VA
// order, and SHA1_Final inlines only its first SHA1_Update call.

#include "../h/sha1.h"
#include <string.h>
#include <stdlib.h>
#include "../gen/globals117.h"

// The block viewed as bytes or as sixteen dwords.
union sha1_block_t {
  BYTE c[64];
  DWORD l[16];
};

#define blk0(i) (block->l[i] = (_rotl(block->l[i], 24) & 0xFF00FF00) | (_rotl(block->l[i], 8) & 0x00FF00FF))
#define blk(i) (block->l[i & 15] = _rotl(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ block->l[(i + 2) & 15] ^ block->l[i & 15], 1))
#define R0(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk0(i) + 0x5A827999 + _rotl(v, 5); w = _rotl(w, 30);
#define R1(v, w, x, y, z, i) z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + _rotl(v, 5); w = _rotl(w, 30);
#define R2(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + _rotl(v, 5); w = _rotl(w, 30);
#define R3(v, w, x, y, z, i) z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + _rotl(v, 5); w = _rotl(w, 30);
#define R4(v, w, x, y, z, i) z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + _rotl(v, 5); w = _rotl(w, 30);

// Pad with 0x80, zeros to 448 mod 512, the big-endian bit count, and read the
// state out big-endian.
void SHA1_Final(void *pvHash, shactx_t *pCtx) {
  BYTE finalcount[8];
  DWORD i;

  for (i = 0; i < 8; i++)
    finalcount[i] = (BYTE)((&pCtx->dwBitsLo)[(i >= 4) ? 0 : 1] >> ((3 - (i & 3)) * 8));
  SHA1_Update(pCtx, "\200", 1);
  while ((pCtx->dwBitsLo & 504) != 448)
    SHA1_Update(pCtx, "\0", 1);
  SHA1_Update(pCtx, finalcount, 8);
  for (i = 0; i < 20; i++)
    ((BYTE *)pvHash)[i] = (BYTE)(pCtx->dwState[i >> 2] >> ((3 - (i & 3)) * 8));
}

// Load the standard initial state and clear the bit count.
void SHA1_Init(shactx_t *pCtx) {
  pCtx->dwState[0] = 0x67452301;
  pCtx->dwState[1] = 0xEFCDAB89;
  pCtx->dwState[2] = 0x98BADCFE;
  pCtx->dwState[3] = 0x10325476;
  pCtx->dwState[4] = 0xC3D2E1F0;
  pCtx->dwBitsHi = 0;
  pCtx->dwBitsLo = 0;
}

// Hash one 64-byte block into the state, byte-swapping the block in place.
void SHA1_Transform(DWORD *pdwState, BYTE *pbBuffer) {
  DWORD a, b, c, d, e;
  sha1_block_t *block = (sha1_block_t *)pbBuffer;

  a = pdwState[0];
  b = pdwState[1];
  c = pdwState[2];
  d = pdwState[3];
  e = pdwState[4];
  R0(a, b, c, d, e, 0); R0(e, a, b, c, d, 1); R0(d, e, a, b, c, 2); R0(c, d, e, a, b, 3);
  R0(b, c, d, e, a, 4); R0(a, b, c, d, e, 5); R0(e, a, b, c, d, 6); R0(d, e, a, b, c, 7);
  R0(c, d, e, a, b, 8); R0(b, c, d, e, a, 9); R0(a, b, c, d, e, 10); R0(e, a, b, c, d, 11);
  R0(d, e, a, b, c, 12); R0(c, d, e, a, b, 13); R0(b, c, d, e, a, 14); R0(a, b, c, d, e, 15);
  R1(e, a, b, c, d, 16); R1(d, e, a, b, c, 17); R1(c, d, e, a, b, 18); R1(b, c, d, e, a, 19);
  R2(a, b, c, d, e, 20); R2(e, a, b, c, d, 21); R2(d, e, a, b, c, 22); R2(c, d, e, a, b, 23);
  R2(b, c, d, e, a, 24); R2(a, b, c, d, e, 25); R2(e, a, b, c, d, 26); R2(d, e, a, b, c, 27);
  R2(c, d, e, a, b, 28); R2(b, c, d, e, a, 29); R2(a, b, c, d, e, 30); R2(e, a, b, c, d, 31);
  R2(d, e, a, b, c, 32); R2(c, d, e, a, b, 33); R2(b, c, d, e, a, 34); R2(a, b, c, d, e, 35);
  R2(e, a, b, c, d, 36); R2(d, e, a, b, c, 37); R2(c, d, e, a, b, 38); R2(b, c, d, e, a, 39);
  R3(a, b, c, d, e, 40); R3(e, a, b, c, d, 41); R3(d, e, a, b, c, 42); R3(c, d, e, a, b, 43);
  R3(b, c, d, e, a, 44); R3(a, b, c, d, e, 45); R3(e, a, b, c, d, 46); R3(d, e, a, b, c, 47);
  R3(c, d, e, a, b, 48); R3(b, c, d, e, a, 49); R3(a, b, c, d, e, 50); R3(e, a, b, c, d, 51);
  R3(d, e, a, b, c, 52); R3(c, d, e, a, b, 53); R3(b, c, d, e, a, 54); R3(a, b, c, d, e, 55);
  R3(e, a, b, c, d, 56); R3(d, e, a, b, c, 57); R3(c, d, e, a, b, 58); R3(b, c, d, e, a, 59);
  R4(a, b, c, d, e, 60); R4(e, a, b, c, d, 61); R4(d, e, a, b, c, 62); R4(c, d, e, a, b, 63);
  R4(b, c, d, e, a, 64); R4(a, b, c, d, e, 65); R4(e, a, b, c, d, 66); R4(d, e, a, b, c, 67);
  R4(c, d, e, a, b, 68); R4(b, c, d, e, a, 69); R4(a, b, c, d, e, 70); R4(e, a, b, c, d, 71);
  R4(d, e, a, b, c, 72); R4(c, d, e, a, b, 73); R4(b, c, d, e, a, 74); R4(a, b, c, d, e, 75);
  R4(e, a, b, c, d, 76); R4(d, e, a, b, c, 77); R4(c, d, e, a, b, 78); R4(b, c, d, e, a, 79);
  pdwState[0] += a;
  pdwState[1] += b;
  pdwState[2] += c;
  pdwState[3] += d;
  pdwState[4] += e;
}

// Append bytes, transforming each block as it fills.
void SHA1_Update(shactx_t *pCtx, const void *pvData, DWORD cbData) {
  const BYTE *data = (const BYTE *)pvData;
  DWORD i, j;

  j = (pCtx->dwBitsLo >> 3) & 63;
  if ((pCtx->dwBitsLo += cbData << 3) < (cbData << 3))
    pCtx->dwBitsHi++;
  pCtx->dwBitsHi += cbData >> 29;
  if (j + cbData > 63) {
    memcpy(&pCtx->bBlock[j], data, (i = 64 - j));
    SHA1_Transform(pCtx->dwState, pCtx->bBlock);
    for (; i + 63 < cbData; i += 64)
      SHA1_Transform(pCtx->dwState, (BYTE *)&data[i]);
    j = 0;
  } else {
    i = 0;
  }
  memcpy(&pCtx->bBlock[j], &data[i], cbData - i);
}
