// CtxtHelp.cpp -- the context-help bubble. Bodies are in oracle VA order.
//
// A callee defined here can be spliced into a caller here by /Ob2, which the
// oracle (WPO off) never does across a TU; that is what the
// __declspec(noinline) markers on some declarations are for, and why moving a
// body between TUs can change the code generated.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stddef.h>      // offsetof, for bmp.h's static_asserts

#include "../h/CtxtHelp.h"
#include "../h/bmp.h"      // bmp_t, bmp_destroy, bmp_clear, bmp_copy_rect
#include "../h/rect16.h"
#include "../h/img.h"      // img_invalid
#include "../h/dlg_dlgdraw.h"  // img_area_invalid
#include "../h/dlg_draw.h"     // font_set, font_set_color_tbl
#include "../h/gluRdy.h"       // bmp_create

// Callees no header of ours declares.

// 0x4CDF20, spelled as src117/lang/dlg_draw.cpp declares it.
const char *__fastcall font_adjust(int nX, int nY, const char *psz);

// 0x418F60 / 0x4191D0, declared in statdata.h and minimap.h respectively, so
// this TU declares them locally rather than in a header of its own.
void __fastcall ctxhelp_show(int nX, int nY, intptr_t nParam);
void __fastcall ctxhelp_hide(void);

// The globals this TU reaches, dual-spelled: one #ifdef per name, never one
// over the block, so adding a name cannot undeclare the ones below it.

// The two bitmaps the bubble owns, both bmp_t objects rather than pointers:
// ctxhelp_close passes their own addresses to bmp_destroy as `mov ecx, 5C998Ch`
// and `mov ecx, 5C9994h`, and tests the pixel base at +4 of each.
#ifdef OC_DLL_MODE
#define _img_help_save (*reinterpret_cast<bmp_t *>(OC_VA(0x5C998Cu)))
#else
extern bmp_t _img_help_save;
#endif

#ifdef OC_DLL_MODE
#define ctxhelp_surface_g (*reinterpret_cast<bmp_t *>(OC_VA(0x5C9994u)))
#else
extern bmp_t ctxhelp_surface_g;
#endif

// Set by img_ctxhelp_invalid and cleared by ctxhelp_hide; every other body in
// the TU opens by testing it. A DWORD: IsCtxHelpDirty is `mov eax, dword ptr
// [0x5C999C]` / `ret`.
#ifdef OC_DLL_MODE
#define sgbCtxhelpInvalid_g (*reinterpret_cast<LONG *>(OC_VA(0x5C999Cu)))
#else
extern LONG sgbCtxhelpInvalid_g;
#endif

// The blit block sub_419330 fills and sub_4192B0 replays: sub_419330 pushes
// 5C99A0h as bmp_copy_rect's fourth argument, which bmp.h types bmpsrc_t, and
// writes all six of its words. Only its first field has a name.
#ifdef OC_DLL_MODE
#define ctxhelp_x_g (*reinterpret_cast<bmpsrc_t *>(OC_VA(0x5C99A0u)))
#else
extern bmpsrc_t ctxhelp_x_g;
#endif

// _img_list @0x6D0B90 (src117/h/img.h) -- eight image layers; this TU owns [1].

// The font's own line-extra byte, cleared by ctxhelp_show.
#ifdef OC_DLL_MODE
#define gbFontLineExtra (*reinterpret_cast<BYTE *>(OC_VA(0x6D3034u)))
#else
extern BYTE gbFontLineExtra;
#endif

// The rectangle font_adjust leaves behind: the extent of the text it just
// measured. A rect16_t: ctxhelp_show reads +0/+4 with dword loads, both
// 4-aligned, and +2/+6 with word loads, so it is one 8-byte object of shorts.
#ifdef OC_DLL_MODE
#define gFontRect (*reinterpret_cast<rect16_t *>(OC_VA(0x6D3040u)))
#else
extern rect16_t gFontRect;
#endif

#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView
#include "../port/hud/port_hudinput.h"  // Hyperion: PortHudHelpPlace, PortHudTipShow
#include <string.h>
#include <string>

// The .rdata owner tag both bmp_create calls push: the string at 0x4EC0C4.
#define OC_CTXTHELP_FILE "CtxtHelp.cpp"

// The bodies, in oracle VA order.

// Blit the bubble's surface onto an arbitrary destination, whole.
void sub_419090(bmp_t *pDest)
{
  bmpsrc_t src;

  if (sgbCtxhelpInvalid_g == 0)
    return;

  src.rc.left = 0;
  src.rc.top = 0;
  src.nWidth = _img_list[1].nWidth;
  src.nHeight = _img_list[1].nHeight;
  src.rc.right = src.nWidth - 1;
  src.rc.bottom = src.nHeight - 1;

  _bmp_current = pDest;
  bmp_copy_rect(ctxhelp_x_g.rc.left, ctxhelp_x_g.rc.top, &ctxhelp_surface_g,
                &src);
  _bmp_current = &_img_draw_buf;
}

// Image layer 1's update hook: copy the bubble into the caller's bitmap at the
// layer's own origin.
void __fastcall ctxhelp_update(unsigned short, unsigned short, bmp_t *pBmp, bmpsrc_t *pSrc)
{
  if (sgbCtxhelpInvalid_g == 0)
    return;
  if (pBmp == NULL)
    return;
  if (pBmp->pPixels == NULL)
    return;

  pSrc->rc.left = 0;
  pSrc->rc.top = 0;
  pSrc->nWidth = _img_list[1].nWidth;
  pSrc->nHeight = _img_list[1].nHeight;
  pSrc->rc.right = pSrc->nWidth - 1;
  pSrc->rc.bottom = pSrc->nHeight - 1;

  bmp_copy_rect(_img_list[1].nX, _img_list[1].nY, pBmp, pSrc);
}

// Frees both bitmaps and marks the bubble gone. Each pixel base doubles as the
// allocated flag, so it is tested before the free and cleared after it, and
// 0x6D0BA4 is this TU's own image layer rather than a global of its own.
void ctxhelp_close()
{
  _img_list[1].bInUse = 0;
  if (_img_help_save.pPixels != NULL) {
    bmp_destroy(&_img_help_save);
    _img_help_save.pPixels = NULL;
  }
  if (ctxhelp_surface_g.pPixels != NULL) {
    bmp_destroy(&ctxhelp_surface_g);
    ctxhelp_surface_g.pPixels = NULL;
  }
}

// The surface object itself, not a pointer to one.
bmp_t *ctxhelp_get_surface()
{
  return &ctxhelp_surface_g;
}

// Retire the bubble: invalidate what it covered and park the layer off screen.
void ctxhelp_hide()
{
  PortHudTipHide();  // Hyperion: the HUD's tooltip goes too
  img_area_invalid(_img_list[1].nX, _img_list[1].nY,
                   _img_list[1].nX + _img_list[1].nWidth - 1,
                   _img_list[1].nY + _img_list[1].nHeight - 1);
  img_invalid(1);
  // Hyperion: parked past the view's right edge, 0x280 in 1.17.
  _img_list[1].nX = static_cast<short>(g_portView.nWidth);
  _img_list[1].nY = static_cast<short>(g_portView.nMapRows);
  sgbCtxhelpInvalid_g = 0;
}

// Build the two bitmaps and hand the surface to image layer 1.
void ctxhelp_init()
{
  sgbCtxhelpInvalid_g = 0;
  ctxhelp_surface_g.nWidth = 0xA0;
  ctxhelp_surface_g.nHeight = 0x5C;
  bmp_create(&ctxhelp_surface_g, OC_CTXTHELP_FILE, 0x104);

  _img_help_save.nWidth = 0xA0;
  _img_help_save.nHeight = 0x5C;
  bmp_create(&_img_help_save, OC_CTXTHELP_FILE, 0x108);

  _img_list[1].pBmp = &ctxhelp_surface_g;
  _img_list[1].nX = 0;
  _img_list[1].nY = 0;
  _img_list[1].nWidth = 0;
  _img_list[1].nHeight = 0;
  _img_list[1].pfnUpdate = ctxhelp_update;
  _img_list[1].bInUse = 1;
  _img_list[1].bFlags = 0;
}

int IsCtxHelpDirty()
{
  return sgbCtxhelpInvalid_g;
}

// Put back what sub_419330 saved, at the saved origin, then reset the block.
void sub_4192B0(bmp_t *pDest, int nX, int nY)
{
  rect16_t rcSave;

  if (sgbCtxhelpInvalid_g == 0)
    return;

  rcSave.left = ctxhelp_x_g.rc.left;
  rcSave.top = ctxhelp_x_g.rc.top;
  ctxhelp_x_g.rc.left = 0;
  ctxhelp_x_g.rc.top = 0;
  ctxhelp_x_g.rc.right = ctxhelp_x_g.nWidth - 1;
  ctxhelp_x_g.rc.bottom = ctxhelp_x_g.nHeight - 1;

  _bmp_current = pDest;
  bmp_copy_rect(rcSave.left, rcSave.top, &_img_help_save, &ctxhelp_x_g);
  _bmp_current = &_img_draw_buf;
}

// Save the screen under the bubble into _img_help_save, and record where from.
// Each derived value is read back out of the block's own fields, and the stores
// run in field order: the rectangle, its extent, then the save bitmap's size.
void sub_419330(bmp_t *pSrc, int nX, int nY)
{
  ctxhelp_x_g.rc.left = _img_list[1].nX - nX;
  ctxhelp_x_g.rc.top = _img_list[1].nY - nY;
  ctxhelp_x_g.rc.right = _img_list[1].nWidth - 1 + ctxhelp_x_g.rc.left;
  ctxhelp_x_g.rc.bottom = _img_list[1].nHeight - 1 + ctxhelp_x_g.rc.top;
  ctxhelp_x_g.nWidth = ctxhelp_x_g.rc.right - ctxhelp_x_g.rc.left + 1;
  ctxhelp_x_g.nHeight = ctxhelp_x_g.rc.bottom - ctxhelp_x_g.rc.top + 1;
  _img_help_save.nWidth = _img_list[1].nWidth;
  _img_help_save.nHeight = _img_list[1].nHeight;

  _bmp_current = &_img_help_save;
  bmp_copy_rect(0, 0, pSrc, &ctxhelp_x_g);
  _bmp_current = &_img_draw_buf;
}

// Paint the bubble: clear it, lay the text out inside it, clamp the on-screen
// rectangle to 640x480 and frame it.
void ctxhelp_show(int nX, int nY, intptr_t nParam)
{
  void *pSaved;
  rect16_t rc;
  int nOver;

  // Hyperion: over the HUD, its style's tooltip in place of the bubble:
  // the first line the name, a short "(key)" at its end the key, the rest the lines under it
  {
    const char *pszText = reinterpret_cast<const char *>(nParam);
    if (pszText != NULL) {
      const char *pszNl = strchr(pszText, '\n');
      std::string name(pszText, pszNl != NULL ? pszNl - pszText : strlen(pszText));
      std::string key;
      const size_t open = name.rfind(" (");
      if (open != std::string::npos && name.size() > open + 3 && name.back() == ')' && name.size() - open <= 11) {
        key = name.substr(open + 2, name.size() - open - 3);
        name.erase(open);
      }
      if (PortHudTipShow(-1, nX, nY, name.c_str(), true, key.c_str(), pszNl != NULL ? pszNl + 1 : NULL, NULL))
        return;
    }
  }

  pSaved = _bmp_current;
  _bmp_current = &ctxhelp_surface_g;
  bmp_clear(sgbPens_g[15]);

  font_set(sgpFonts_g[0]);
  font_set_color_tbl(2);
  gbFontJust = 0x11;
  gsFontMargins.left = 2;
  gsFontMargins.top = 0;
  gsFontMargins.right = ctxhelp_surface_g.nWidth - 1;
  gsFontMargins.bottom = ctxhelp_surface_g.nHeight - 1;
  gbFontLineExtra = 0;
  font_adjust(2, 0, reinterpret_cast<const char *>(nParam));
  font_set(0);

  if (nX < 0)
    nX = 0;
  rc.left = nX;
  rc.right = gFontRect.right - gFontRect.left + nX;
  nOver = g_portView.nWidth - 1 - rc.right;  // Hyperion: the view's edge, 0x27F
  if (nOver < 0) {
    rc.left = nX + nOver;
    rc.right = rc.right + nOver;
  }

  if (nY < 0)
    nY = 0;
  rc.top = nY;
  rc.bottom = gFontRect.bottom - gFontRect.top + nY;
  nOver = g_portView.nHeight - 1 - rc.bottom;  // Hyperion: the view's edge, 0x1DF
  if (nOver < 0) {
    rc.top = nY + nOver;
    rc.bottom = rc.bottom + nOver;
  }
  PortHudHelpPlace(nX, nY, &rc);  // Hyperion: off the HUD's panels

  _bmp_pen = sgbPens_g[16];
  bmp_rect(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1);

  _bmp_current = pSaved;
  img_ctxhelp_invalid(&rc);
}

// Mark the area the bubble is moving off, adopt the new rectangle and mark
// that too.
void img_ctxhelp_invalid(const rect16_t *prc)
{
  if (sgbCtxhelpInvalid_g != 0) {
    img_area_invalid(_img_list[1].nX, _img_list[1].nY,
                     _img_list[1].nX + _img_list[1].nWidth - 1,
                     _img_list[1].nY + _img_list[1].nHeight - 1);
  }

  _img_list[1].nX = prc->left;
  _img_list[1].nY = prc->top;
  _img_list[1].nWidth = prc->right - prc->left + 1;
  _img_list[1].nHeight = prc->bottom - prc->top + 1;
  img_invalid(1);
  sgbCtxhelpInvalid_g = 1;
}
