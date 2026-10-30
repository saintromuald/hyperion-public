// compress.cpp -- PKWARE implode/explode wrappers. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/compress.h"

#include <string.h>  // memcpy -- both callbacks copy through it

#include "../h/Storm.h"     // SMemAlloc / SMemFree
#include "../h/fd_user.h"   // comp_crc32
#include "../h/saveload.h"  // compress / decompress / pfread / pfwrite
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it. All three
// are scalar pointers, not arrays: the oracle loads their contents and comp_free
// stores 0 back into them.

// 0x5A52B0. The 0x18-byte callback state block, SMemAlloc'd by compress and
// decompress and read back through esi by comp_implode and comp_explode.
#ifndef OC_GPINFO
#define OC_GPINFO
#ifdef OC_DLL_MODE
#define gpInfo (*reinterpret_cast<comp_info_t **>(OC_VA(0x5A52B0u)))
#else
extern comp_info_t *gpInfo;
#endif
#endif

// 0x5A52B4. The PKWARE work buffer, 0x8DD8 bytes for implode and 0x3134 for
// explode; passed straight through as the library's third argument.
#ifndef OC_GPPKWORKSPACE
#define OC_GPPKWORKSPACE
#ifdef OC_DLL_MODE
#define gpPKWorkSpace (*reinterpret_cast<char **>(OC_VA(0x5A52B4u)))
#else
extern char *gpPKWorkSpace;
#endif
#endif

// 0x5A52B8. The 0x2000-byte staging buffer both wrappers hand to the library as
// comp_info_t::pDst and hand back to the caller through *ppDst.
#ifndef OC_GPDESTBUFFER
#define OC_GPDESTBUFFER
#ifdef OC_DLL_MODE
#define gpDestBuffer (*reinterpret_cast<char **>(OC_VA(0x5A52B8u)))
#else
extern char *gpDestBuffer;
#endif
#endif

// The bodies, in oracle VA order.

// 0x413750. The PKWARE read callback: hand the library up to *pnSize bytes and
// report how many it actually got. `cmovb` on the clamp is why both the request
// and the remaining count are unsigned.
unsigned int __cdecl read_buf(char *pBuf, unsigned int *pnSize, void *pParam)
{
  comp_info_t *pInfo = static_cast<comp_info_t *>(pParam);
  unsigned int nWant = *pnSize;
  unsigned int nMove = pInfo->nSrcSize - pInfo->nSrcPos;

  if (nWant < nMove)
    nMove = nWant;

  memcpy(pBuf, pInfo->pSrc + pInfo->nSrcPos, nMove);
  pInfo->nSrcPos += nMove;
  return nMove;
}

// 0x413790. The PKWARE write callback. A block that would overrun nDstSize is
// dropped, but nDstPos still advances past it, which is how comp_implode and
// comp_explode detect the overflow afterwards.
void __cdecl write_buf(char *pBuf, unsigned int *pnSize, void *pParam)
{
  comp_info_t *pInfo = static_cast<comp_info_t *>(pParam);

  if (pInfo->nDstPos + *pnSize <= pInfo->nDstSize)
    memcpy(pInfo->pDst + pInfo->nDstPos, pBuf, *pnSize);

  pInfo->nDstPos += *pnSize;
}

// Hyperion: the work buffers are pklib's sizes (port_pklib.cpp): 0x8DD8 and
// 0x3134 in 1.17 and on x86, larger on x64, where pklib's structures hold wider
// pointers.
extern const unsigned int g_pkImplodeWorkSize;
extern const unsigned int g_pkExplodeWorkSize;

// 0x4137D0. CRC the whole block, write it and the block count, then implode
// 0x2000 bytes at a time. Every failure path calls comp_free and returns 0,
// including the two before the buffers exist -- comp_free nulls what it frees,
// so the second free is of a null pointer.
int __fastcall compress(const void *pSrc, int nSrcLen, void *pDstFile)
{
  const char *pCur = static_cast<const char *>(pSrc);
  int nTotal = 0;
  int nCrc;

  if (nSrcLen == 0)
    return 0;

  {
    unsigned int nCrcLen = static_cast<unsigned int>(nSrcLen);
    unsigned int dwCrc = 0xFFFFFFFFu;
    nCrc = static_cast<int>(crc32_pklib(pSrc, &nCrcLen, &dwCrc));
    if (!pfwrite(&nCrc, 4, pDstFile))
      return 0;
  }

  int nBlocks;
  nBlocks = static_cast<int>(static_cast<unsigned int>(nSrcLen) >> 13);
  if (nSrcLen - (nBlocks << 13) != 0)
    nBlocks++;

  if (!pfwrite(&nBlocks, 4, pDstFile))
    goto fail;

  gpInfo = static_cast<comp_info_t *>(SMemAlloc(sizeof(comp_info_t), "compress.cpp", 48, 0));
  gpPKWorkSpace = static_cast<char *>(SMemAlloc(g_pkImplodeWorkSize, "compress.cpp", 50, 0));
  gpDestBuffer = static_cast<char *>(SMemAlloc(0x2000, "compress.cpp", 53, 0));

  for (int i = 0; i < nBlocks; i++) {
    unsigned int nChunk = static_cast<unsigned int>(nSrcLen - i * 0x2000);
    if (nChunk > 0x2000)
      nChunk = 0x2000;

    void *pBlock;
    int nOut = comp_implode(pCur, nChunk, &pBlock);
    if (nOut == 0)
      goto fail;

    pCur += nChunk;
    nTotal += nOut;

    if (!pfwrite(&nOut, 4, pDstFile))
      goto fail;
    if (!pfwrite(pBlock, nOut, pDstFile))
      goto fail;
  }

  comp_free();
  return nTotal;

fail:
  comp_free();
  return 0;
}

// 0x413940. Implode one block into gpDestBuffer. A block the library failed on,
// or one that did not get smaller, is handed back as the source itself.
int comp_implode(const void *pSrc, unsigned int nLen, void **ppDst)
{
  char *pDstBuf = gpDestBuffer;
  comp_info_t *pInfo = gpInfo;

  pInfo->pSrc = static_cast<const char *>(pSrc);
  pInfo->nSrcPos = 0;
  pInfo->pDst = pDstBuf;
  pInfo->nDstPos = 0;
  pInfo->nSrcSize = nLen;
  pInfo->nDstSize = nLen;

  unsigned int nType = 0;
  unsigned int nDictSize = 0x400;

  if (implode(read_buf, write_buf, gpPKWorkSpace, pInfo, &nType, &nDictSize) ==
      0) {
    comp_info_t *pOut = gpInfo;
    if (pOut->nDstPos < nLen) {
      *ppDst = gpDestBuffer;
      return static_cast<int>(pOut->nDstPos);
    }
  }

  *ppDst = const_cast<void *>(pSrc);
  return static_cast<int>(nLen);
}

// 0x4139E0. Free the three buffers and null them. The owner tag and lines are
// literals, the oracle's; __FILE__/__LINE__ would follow this file's layout.
void comp_free()
{
  SMemFree(gpInfo, "compress.cpp", 58, 0);
  SMemFree(gpPKWorkSpace, "compress.cpp", 59, 0);
  SMemFree(gpDestBuffer, "compress.cpp", 60, 0);
  gpInfo = NULL;
  gpPKWorkSpace = NULL;
  gpDestBuffer = NULL;
}

// 0x413A40. Explode one block into gpDestBuffer. The destination is fixed at
// 0x2000, and an output that reached past it is reported as failure.
int comp_explode(const void *pSrc, unsigned int nLen, void **ppDst)
{
  char *pDstBuf = gpDestBuffer;
  // __restrict: in the payload gpPKWorkSpace is a literal address and its read
  // must not wait for the stores through pInfo.
  comp_info_t *__restrict pInfo = gpInfo;

  pInfo->pSrc = static_cast<const char *>(pSrc);
  pInfo->nSrcPos = 0;
  pInfo->pDst = pDstBuf;
  pInfo->nDstPos = 0;
  pInfo->nSrcSize = nLen;
  pInfo->nDstSize = 0x2000;

  if (explode(read_buf, write_buf, gpPKWorkSpace, pInfo) != 0)
    return 0;

  comp_info_t *pOut = gpInfo;
  if (pOut->nDstPos > 0x2000)
    return 0;

  *ppDst = gpDestBuffer;
  return static_cast<int>(pOut->nDstPos);
}

// 0x413AB0. CRC a block with the seed and length the library wants by address.
// Both arguments are spelled int because fd_user.h already seats the name.
int comp_crc32(intptr_t nArg0, int nArg1)
{
  unsigned int dwCrc = 0xFFFFFFFFu;

  return static_cast<int>(
      crc32_pklib(reinterpret_cast<const void *>(nArg0),
                  reinterpret_cast<unsigned int *>(&nArg1), &dwCrc));
}

// 0x413AE0. Read the CRC and block count, explode each block into place, then
// re-CRC what was written and return whether it agreed. The length check is
// against what is left of the caller's buffer, so a corrupt block count cannot
// run past it.
int __fastcall decompress(void *pDst, int nDstLen, void *pSrcFile)
{
  char *pCur;
  int nTotal = 0;

  if (nDstLen == 0)
    return 0;

  unsigned int dwWantCrc;
  if (!pfread(&dwWantCrc, 4, pSrcFile))
    return 0;

  int nBlocks;
  if (!pfread(&nBlocks, 4, pSrcFile))
    return 0;

  gpInfo = static_cast<comp_info_t *>(SMemAlloc(sizeof(comp_info_t), "compress.cpp", 48, 0));
  gpPKWorkSpace = static_cast<char *>(SMemAlloc(g_pkExplodeWorkSize, "compress.cpp", 52, 0));
  gpDestBuffer = static_cast<char *>(SMemAlloc(0x2000, "compress.cpp", 53, 0));

  pCur = static_cast<char *>(pDst);
  for (int i = 0; i < nBlocks; i++) {
    void *pBlock = NULL;
    unsigned int nIn;
    if (!pfread(&nIn, 4, pSrcFile))
      goto fail;

    unsigned int nLeft = static_cast<unsigned int>(nDstLen - nTotal);
    if (nIn > nLeft)
      goto fail;
    if (!pfread(pCur, static_cast<int>(nIn), pSrcFile))
      goto fail;

    unsigned int nRoom = static_cast<unsigned int>(nDstLen - i * 0x2000);
    if (nRoom > 0x2000)
      nRoom = 0x2000;

    unsigned int nOut = nIn;
    if (nIn != nRoom) {
      nOut = static_cast<unsigned int>(comp_explode(pCur, nIn, &pBlock));
      if (nOut == 0)
        goto fail;
      if (nOut > nLeft)
        goto fail;
      memcpy(pCur, pBlock, nOut);
    }

    pCur += nOut;
    nTotal += nOut;
  }

  comp_free();
  if (nDstLen != nTotal)
    return 0;

  {
    unsigned int nLen = static_cast<unsigned int>(nDstLen);
    unsigned int dwCrc = 0xFFFFFFFFu;
    return crc32_pklib(pDst, &nLen, &dwCrc) == dwWantCrc;
  }

fail:
  comp_free();
  return 0;
}
