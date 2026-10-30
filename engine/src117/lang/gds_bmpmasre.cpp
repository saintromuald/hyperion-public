// gds_bmpmasre.cpp -- the clipped transparent-colour blit onto the current
// bitmap.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>
#include "../h/gds_bmpmasre.h"
#include "../h/gds_bmpcopre.h"
#include "../gen/globals117.h"
#include "../port/render/port_scene.h"  // Hyperion: PortSceneBlit

// The six blit parameters bmp_mask_rect_asm reads, 0x50F2F0 to 0x50F307, each
// 32 bits, dual-spelled with one #ifdef per name.
#ifndef OC_GL_BLT_WDT
#define OC_GL_BLT_WDT
#ifdef OC_DLL_MODE
#define glBltWdt (*reinterpret_cast<int *>(OC_VA(0x50F2F0u)))
#else
extern int glBltWdt;
#endif
#endif
#ifndef OC_GL_BLT_HGT
#define OC_GL_BLT_HGT
#ifdef OC_DLL_MODE
#define glBltHgt (*reinterpret_cast<int *>(OC_VA(0x50F2F4u)))
#else
extern int glBltHgt;
#endif
#endif
#ifndef OC_GL_BLT_SRC_MOD
#define OC_GL_BLT_SRC_MOD
#ifdef OC_DLL_MODE
#define glBltSrcMod (*reinterpret_cast<int *>(OC_VA(0x50F2F8u)))
#else
extern int glBltSrcMod;
#endif
#endif
#ifndef OC_GL_BLT_DST_MOD
#define OC_GL_BLT_DST_MOD
#ifdef OC_DLL_MODE
#define glBltDstMod (*reinterpret_cast<int *>(OC_VA(0x50F2FCu)))
#else
extern int glBltDstMod;
#endif
#endif
#ifndef OC_GP_BLT_SRC
#define OC_GP_BLT_SRC
#ifdef OC_DLL_MODE
#define gpBltSrc (*reinterpret_cast<BYTE * *>(OC_VA(0x50F300u)))
#else
extern BYTE * gpBltSrc;
#endif
#endif
#ifndef OC_GP_BLT_DST
#define OC_GP_BLT_DST
#ifdef OC_DLL_MODE
#define gpBltDst (*reinterpret_cast<BYTE * *>(OC_VA(0x50F304u)))
#else
extern BYTE * gpBltDst;
#endif
#endif

// 0x4CF5E0. Clips the pSrc rectangle of pBmp against both bitmaps, loads the
// six blit globals and lets bmp_mask_rect_asm copy every nonzero pixel.
void bmp_mask_rect(unsigned short nX, unsigned short nY, bmp_t *pBmp,
                   bmpsrc_t *pSrc)
{
  bmp_t *pCur = static_cast<bmp_t *>(_bmp_current);
  bmpsrc_t src = *pSrc;
  bmpsrc_t dst;
  int nWidth;

  src.nWidth = pBmp->nWidth;
  src.nHeight = pBmp->nHeight;
  dst.rc.left = nX;
  dst.rc.top = nY;
  dst.nWidth = pCur->nWidth;
  dst.nHeight = pCur->nHeight;
  if (img_clipped(&src, &dst))
    return;
  gpBltSrc = pBmp->pPixels;
  gpBltSrc += src.rc.left + src.rc.top * pBmp->nWidth;
  gpBltDst = pCur->pPixels;
  gpBltDst += dst.rc.left + dst.rc.top * pCur->nWidth;
  // The width is loaded once, into a local, as 1.17 does.
  nWidth = src.nWidth;
  glBltSrcMod = pBmp->nWidth - nWidth;
  glBltDstMod = pCur->nWidth - nWidth;
  glBltWdt = nWidth;
  glBltHgt = src.nHeight;
  if (!PortSceneBlit(gpBltDst, pCur->nWidth, gpBltSrc, pBmp->nWidth, nWidth, src.nHeight, NULL, 1))  // Hyperion: the GPU scene
    bmp_mask_rect_asm();
}
