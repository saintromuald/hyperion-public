// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNIcon.cpp -- the battle.net list icons. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBNIcon.h"
#include <string.h>
#include "../h/gluBN.h"           // blt_size_t, blt_rect_t, LoadArtFile
#include "../h/Storm.h"           // SMemFree, SBltROP3Clipped, SDlgBltToWindowE
#include "../gen/globals117.h"

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// The fixed icon size bn_icons_get_size hands out.
#ifdef OC_DLL_MODE
#define dword_5034C0 (*reinterpret_cast<blt_size_t *>(OC_VA(0x5034C0u)))
#else
extern blt_size_t dword_5034C0;
#endif
// The icon strip's pixels, its size and how many icons one column holds.
#ifdef OC_DLL_MODE
#define sgpIconArtFile_g (*reinterpret_cast<BYTE **>(OC_VA(0x667AF0u)))
#else
extern BYTE *sgpIconArtFile_g;
#endif
#ifdef OC_DLL_MODE
#define sgIconArtSize_g (*reinterpret_cast<blt_size_t *>(OC_VA(0x667AF4u)))
#else
extern blt_size_t sgIconArtSize_g;
#endif
#ifdef OC_DLL_MODE
#define sgNumVerticalIcons_g (*reinterpret_cast<unsigned int *>(OC_VA(0x667AFCu)))
#else
extern unsigned int sgNumVerticalIcons_g;
#endif

// The bodies, in oracle VA order.

// Computes icon nIcon's 28x14 cell in the strip, clipped to the strip. A strip
// with no rows gives column 0 rather than a divide by zero.
void sub_44DEB0(blt_rect_t *prc, unsigned int nIcon) {
  unsigned int nCol = sgNumVerticalIcons_g ? nIcon / sgNumVerticalIcons_g : 0;
  int nX = nCol * 28;
  if (sgIconArtSize_g.cx < nX)
    nX = sgIconArtSize_g.cx;
  int nY = (nIcon - sgNumVerticalIcons_g * nCol) * 14;
  prc->nLeft = nX;
  if (sgIconArtSize_g.cy < nY)
    nY = sgIconArtSize_g.cy;
  prc->nTop = nY;
  int nRight = nX + 28;
  if (sgIconArtSize_g.cx < nRight)
    nRight = sgIconArtSize_g.cx;
  prc->nRight = nRight;
  int nBottom = nY + 14;
  if (sgIconArtSize_g.cy < nBottom)
    nBottom = sgIconArtSize_g.cy;
  prc->nBottom = nBottom;
}

// Frees the icon strip.
void bn_icons_free() {
  if (sgpIconArtFile_g) {
    SMemFree(sgpIconArtFile_g, "gluBNIcon.cpp", 0x6A, 0);
    sgpIconArtFile_g = NULL;
  }
}

// Blits icon nIcon into a surface.
void sub_44DF60(void *pDest, blt_rect_t *prcDst, blt_size_t *pSizeDst,
                int nPitch, int nIcon) {
  blt_rect_t rc;
  sub_44DEB0(&rc, nIcon);
  SBltROP3Clipped(reinterpret_cast<DWORD_PTR>(pDest), reinterpret_cast<DWORD_PTR>(prcDst),
                  reinterpret_cast<DWORD_PTR>(pSizeDst), nPitch,
                  reinterpret_cast<DWORD_PTR>(sgpIconArtFile_g), &rc, &sgIconArtSize_g,
                  sgIconArtSize_g.cx, 0, 0xCC0020);
}

// Draws icon nIcon into a window at (nX, nY).
void sub_44DFB0(void *hWnd, int nX, int nY, int nIcon) {
  blt_rect_t rc;
  sub_44DEB0(&rc, nIcon);
  SDlgBltToWindowE(reinterpret_cast<DWORD_PTR>(hWnd), nX, nY,
                   reinterpret_cast<DWORD_PTR>(sgpIconArtFile_g), &rc,
                   &sgIconArtSize_g, 0xFFFFFFFF);
}

// Returns the icon size, the same for every icon.
blt_size_t *bn_icons_get_size(int nIcon) {
  return &dword_5034C0;
}

// Loads the icon strip, or leaves it empty on failure. The size is cleared with
// memset, one 8-byte store as in 1.17.
void bn_icons_init() {
  if (!LoadArtFile(NULL, NULL, "IconTable", -1, 1,
                   "glue\\battle.net\\icons\\icons.pcx",
                   reinterpret_cast<void **>(&sgpIconArtFile_g),
                   &sgIconArtSize_g, 0)) {
    sgpIconArtFile_g = NULL;
    memset(&sgIconArtSize_g, 0, sizeof(sgIconArtSize_g));
  }
  sgNumVerticalIcons_g = sgIconArtSize_g.cy / 14;
}
