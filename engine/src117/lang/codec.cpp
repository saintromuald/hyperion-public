// codec.cpp -- the file decoder built on IDEA and SHA. Bodies are in oracle VA
// order.

#include "../h/codec.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../h/Storm.h"           // SecureZeroMemory, ERROR_INVALID_PARAMETER
#include "../h/idea.h"
#include "../h/sha.h"
#include "../gen/globals117.h"

// Storm ordinal 465; not in Storm.h, spelled as chunk.cpp spells it.
extern "C" void __stdcall SErrSetLastError(DWORD dwErrCode);

// The bodies, in oracle VA order.

// Decrypts a file image in place, 64 bytes at a time, and checks its trailer.
// One 0x80-byte buffer holds both blocks. The 0/-1 ternaries, the `<= 0` on a
// BYTE and the success path as the else are 1.17's shape.
int DecodeFile(void *pvData, DWORD dwSize, intptr_t nKey) {
  BYTE abHash[20];
  BYTE abBuf[0x80];
  DWORD adwCheck[5];
  if (!((pvData ? -1 : 0) & (nKey ? -1 : 0))) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return 0;
  }
  InitializeKeys(0, reinterpret_cast<const char *>(nKey));
  if (dwSize <= 8)
    return 0;
  dwSize -= 8;
  DWORD dwBody = dwSize;
  if (dwSize & 0x3F)
    return 0;
  DWORD dwLeft = dwSize;
  BYTE *p = static_cast<BYTE *>(pvData);
  int nBlock = 0;
  if (dwLeft) {
    int nHashLen = 20;
    do {
      memcpy(abBuf, p, 0x40);
      ShaGetLastHash(0, reinterpret_cast<DWORD *>(abHash));
      if (!(nBlock & 7)) {
        for (int i = 0; i < 0x40; i += 8)
          IdeaEncrypt(0, reinterpret_cast<intptr_t>(&abBuf[i]), reinterpret_cast<intptr_t>(&abBuf[0x40 + i]));
      } else {
        memcpy(&abBuf[0x40], abBuf, 0x40);
      }
      for (int i = 0; i < 0x40; i++)
        abBuf[i] = abBuf[0x40 + i] - abHash[(0x3F - i) % nHashLen];
      if (!(nBlock & 0xF))
        ShaHash(0, abBuf, NULL);
      SecureZeroMemory(abHash, sizeof(abHash));
      memcpy(p, abBuf, 0x40);
      nBlock++;
      dwLeft -= 0x40;
      p += 0x40;
    } while (dwLeft);
  }
  SecureZeroMemory(abBuf, sizeof(abBuf));
  if (p[4] <= 0) {
    ShaGetLastHash(0, adwCheck);
    if (*reinterpret_cast<DWORD *>(p) != adwCheck[0]) {
      SecureZeroMemory(adwCheck, sizeof(adwCheck));
    } else {
      DWORD dwResult = dwBody + p[5] - 0x40;
      IdeaDestroy();
      ShaDestroy();
      return dwResult;
    }
  }
  IdeaDestroy();
  ShaDestroy();
  return 0;
}

// Derives the three cipher and hash contexts from a fixed-seed rand() block
// mixed with the key's SHA hash, then reseeds rand() from the clock. The sizeof
// bounds make the XOR loop's compare and modulo unsigned.
void InitializeKeys(int bEncrypt, const char *pszKey) {
  BYTE abHash[20];
  BYTE abKey[0x40];
  BYTE abRand[0x70];
  if (!pszKey) {
    SErrSetLastError(ERROR_INVALID_PARAMETER);
    return;
  }
  OcSrand(0x4FA7);
  BYTE *pRand = abRand;
  for (int n = sizeof(abRand); n; n--)
    *pRand++ = OcRand();
  OcSrand((unsigned int)time(NULL));
  int k = 0;
  for (int i = 0; i < 0x40; i++) {
    if (!pszKey[k])
      k = 0;
    abKey[i] = pszKey[k++];
  }
  ShaInitialize(0);
  ShaHash(0, abKey, reinterpret_cast<DWORD *>(abHash));
  ShaDestroy();
  SecureZeroMemory(abKey, sizeof(abKey));
  for (int i = 0; i < sizeof(abRand); i++)
    abRand[i] ^= abHash[i % sizeof(abHash)];
  SecureZeroMemory(abHash, sizeof(abHash));
  for (int j = 0; j < 3; j++) {
    IdeaInitialize(j, bEncrypt, &abRand[j * 16]);
    ShaInitialize(j);
    ShaHash(j, &abRand[0x30], NULL);
  }
  SecureZeroMemory(abRand, sizeof(abRand));
}
