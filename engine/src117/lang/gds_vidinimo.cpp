// gds_vidinimo.cpp -- video mode setup and teardown, gamma, and the path from a
// palette to the monitor. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <math.h>
#include <string.h>
#include "../h/gds_vidinimo.h"
#include "../h/gds_vidinimo_PC.h"
#include "../h/bmp.h"
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: PORT_VIEW_MAX_WIDTH, PORT_VIEW_MAX_HEIGHT, g_portView

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// sglGamma_g, 0x50F2EC: the gamma as a percentage. A DWORD, so set_gamma's
// clamp and apply_gamma_palette's 200 minus it are unsigned.
#ifdef OC_DLL_MODE
#define sglGamma_g (*reinterpret_cast<DWORD *>(OC_VA(0x50F2ECu)))
#else
extern DWORD sglGamma_g;
#endif

// gbVidInit_g, 0x6D3180: a whole dword, 1 from vid_init_mode to deinit_video.
#ifdef OC_DLL_MODE
#define gbVidInit_g (*reinterpret_cast<BOOL *>(OC_VA(0x6D3180u)))
#else
extern BOOL gbVidInit_g;
#endif

// Declared as gluRdy.h declares it, a header this TU does not otherwise need.
void bmp_create(bmp_t *pBmp, const char *pszFile, int nLine);

// The bodies, in oracle VA order.

// apply_gamma_palette -- 0x4CF760. Copies nCount entries through the gamma
// curve; at 100 percent it is a plain copy. The fourth byte is not written.
__declspec(noinline)
void apply_gamma_palette(palentry_t *pDst, const palentry_t *pSrc, int nCount)
{
  if (sglGamma_g == 100) {
    memcpy(pDst, pSrc, nCount * 4);
    return;
  }
  double dExp = (200 - sglGamma_g) / 100.0;
  int n = nCount;
  if (n > 0) {
    do {
      pDst->bRed = static_cast<BYTE>(static_cast<int>(pow(pSrc->bRed / 256.0, dExp) * 256.0));
      pDst->bGreen = static_cast<BYTE>(static_cast<int>(pow(pSrc->bGreen / 256.0, dExp) * 256.0));
      pDst->bBlue = static_cast<BYTE>(static_cast<int>(pow(pSrc->bBlue / 256.0, dExp) * 256.0));
      pDst++;
      pSrc++;
    } while (--n);
  }
}

// get_gamma -- 0x4CF870.
int get_gamma()
{
  return sglGamma_g;
}

// vid_init_mode -- 0x4CF880. Sizes the 640x480 draw buffer, allocates it and
// brings up DirectDraw. Hyperion: the draw buffer is allocated for the
// widest view and sized as the view (port_view.cpp), and the screen is
// port_present.cpp's.
void vid_init_mode()
{
  _img_draw_buf.nWidth = static_cast<short>(PORT_VIEW_MAX_WIDTH);
  _img_draw_buf.nHeight = static_cast<short>(PORT_VIEW_MAX_HEIGHT);
  _img_draw_buf.pPixels = 0;
  bmp_create(&_img_draw_buf, "vidinimo.cpp", 55);
  _img_draw_buf.nWidth = static_cast<short>(g_portView.nWidth);
  _img_draw_buf.nHeight = static_cast<short>(g_portView.nHeight);
  init_directx();
  gbVidInit_g = TRUE;
}

// palette_to_monitor -- 0x4CF8C0. Gamma-corrects into a local copy unless the
// gamma is 100, then hands the entries to the hardware palette.
void __fastcall palette_to_monitor(palentry_t *pEntries, int nFirst, int nCount)
{
  palentry_t buf[256];

  if (sglGamma_g != 100) {
    apply_gamma_palette(&buf[nFirst], pEntries, nCount);
    pEntries = &buf[nFirst];
  }
  flip_palette(pEntries, nFirst, nCount);
}

// deinit_video -- 0x4CF900.
void deinit_video()
{
  vid_init_cycles();
  gbVidInit_g = FALSE;
  free_directx();
  if (_img_draw_buf.pPixels)
    bmp_destroy(&_img_draw_buf);
}

// set_gamma -- 0x4CF930. Out-of-range values above 140 reset to 100; values
// below 60 also become 100.
void set_gamma(DWORD dwGamma)
{
  if (dwGamma > 140) {
    sglGamma_g = 100;
    return;
  }
  if (dwGamma < 60)
    dwGamma = 100;
  sglGamma_g = dwGamma;
}
