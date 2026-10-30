// gds_metabmp.cpp -- allocate and release a bmp_t's pixel block, the two bodies
// of 1.17's TU, in oracle VA order.

#include "../h/gds_metabmp.h"
#include "../h/gluRdy.h"       // bmp_create is declared there, not in bmp.h
#include "../h/Storm.h"        // SMemAlloc, SMemFree (pulls <windows.h>)

#include "../gen/globals117.h"

// 0x4CB200 (36 B). Sizes the block from the descriptor's own dimensions, so the
// caller fills nWidth/nHeight first. The owner tag is the caller's file and
// line, so this takes three arguments and not one.
void bmp_create(bmp_t *pBmp, const char *pszFile, int nLine)
{
  pBmp->pPixels = static_cast<unsigned char *>(
      SMemAlloc(pBmp->nHeight * pBmp->nWidth, pszFile, nLine, 0));
}

// 0x4CB230 (31 B). Frees with a null owner tag and line 0, unlike bmp_create.
void __fastcall bmp_destroy(bmp_t *pBmp)
{
  if (pBmp->pPixels != 0) {
    SMemFree(pBmp->pPixels, 0, 0, 0);
    pBmp->pPixels = 0;
  }
}
