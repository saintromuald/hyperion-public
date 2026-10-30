// header.cpp -- the map header read, one body.

#include "../h/header.h"
#include "../gen/globals117.h"

#include <stdlib.h>   // _rotl / _rotr -- the two rotate opcodes

// 0x464560. Runs the opcode string at pData+0x400 over every dword of pSeed and
// returns the accumulated digest. The first 0x400 bytes of pData are a table of
// 256 dwords the opcodes index with the seed dword's four bytes.
int sub_464560(void *pData, int nSize, void *pSeed, int nSeedSize)
{
  DWORD *pTbl  = (DWORD *)pData;
  DWORD  dwAcc = 0;

  for (DWORD cb = nSeedSize; cb >= 4; cb -= 4) {
    DWORD dwSeed = *(DWORD *)pSeed;
    BYTE *pOp = (BYTE *)pData + 0x400;
    for (; pOp - (BYTE *)pData < nSize; pOp++) {
      switch (*pOp) {
      case 0:
        dwAcc ^= dwSeed;
        break;
      case 1:
        dwAcc += dwSeed;
        break;
      case 2:
        dwAcc -= dwSeed;
        break;
      case 3:
        dwAcc = pTbl[(dwSeed >> 8) & 0xFF] ^ pTbl[(dwSeed >> 24) & 0xFF] ^ pTbl[dwSeed & 0xFF] ^ dwAcc ^ pTbl[(dwSeed >> 16) & 0xFF];
        break;
      case 4:
        dwAcc = pTbl[(dwSeed >> 8) & 0xFF] ^ pTbl[(dwSeed >> 24) & 0xFF] ^ pTbl[dwSeed & 0xFF] ^ dwAcc ^ pTbl[(dwSeed >> 16) & 0xFF];
        break;
      case 5:
        dwAcc = pTbl[(dwSeed >> 8) & 0xFF] ^ pTbl[(dwSeed >> 24) & 0xFF] ^ pTbl[dwSeed & 0xFF] ^ dwAcc ^ pTbl[(dwSeed >> 16) & 0xFF];
        break;
      case 6:
        dwAcc = _rotl(dwAcc, dwSeed & 0x1F);
        dwAcc = _rotl(dwAcc, (dwSeed >> 8) & 0x1F);
        dwAcc = _rotl(dwAcc, (dwSeed >> 16) & 0x1F);
        dwAcc = _rotl(dwAcc, (dwSeed >> 24) & 0x1F);
        break;
      case 7:
        dwAcc = _rotr(dwAcc, dwSeed & 0x1F);
        dwAcc = _rotr(dwAcc, (dwSeed >> 8) & 0x1F);
        dwAcc = _rotr(dwAcc, (dwSeed >> 16) & 0x1F);
        dwAcc = _rotr(dwAcc, (dwSeed >> 24) & 0x1F);
        break;
      }
    }
    pSeed = (DWORD *)pSeed + 1;
  }
  return (int)dwAcc;
}
