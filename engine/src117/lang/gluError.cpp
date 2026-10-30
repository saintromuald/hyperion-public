// gluError.cpp -- the glue shell's modal error screens: the OK and OK/Cancel
// dialogs, their event handler, and the image and binary loaders behind them.
// Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <string.h>

#include "../h/platform.h"
#include "../h/dialog.h"
#include "../h/gluError.h"
#include "../h/gluCmpgn.h"    // do_err_okcancel_dlg
#include "../h/glues.h"       // deactivate_modal_popup
#include "../h/dlg_ctrl.h"
#include "../h/dlg_dlg.h"
#include "../h/dlg_draw.h"    // font_strlen, font_set
#include "../h/gds_local.h"   // GetResource
#include "../h/gluPopup.h"   // popup_txt_update_fcn, do_pok_cancel_dlg
#include "../h/Storm.h"
#include "../gen/globals117.h"
#include "../port/render/port_view.h"  // Hyperion: g_portView

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x669750 -- the modal error popup, dlg_load's result.
// deactivate_modal_popup hands it to dlg_deactivate, so a ctrl_t *.
#ifdef OC_DLL_MODE
#define sgpModalPopup (*reinterpret_cast<ctrl_t **>(OC_VA(0x669750u)))
#else
extern ctrl_t *sgpModalPopup;
#endif

// 0x669754 -- the event-function table the next popup installs, and 0x669A5C
// its size in bytes. err_dlg_init passes the pair to ctrl_set_evt_fcns, which
// dlg_ctrl.h types (PFN_CTRL_EVT *, DWORD).
#ifdef OC_DLL_MODE
#define gErrPopupFcnTbl_g (*reinterpret_cast<PFN_CTRL_EVT **>(OC_VA(0x669754u)))
#define gdwErrPopupTblSize_g (*reinterpret_cast<DWORD *>(OC_VA(0x669A5Cu)))
#else
extern PFN_CTRL_EVT *gErrPopupFcnTbl_g;
extern DWORD gdwErrPopupTblSize_g;
#endif

// 0x669758 / 0x669858 -- the message and statement text the popup displays,
// filled by an inline copy and handed to ctrl_t::pData. 0x100 each, the span to
// the next named global.
#ifdef OC_DLL_MODE
#define gszErrMessage_g (reinterpret_cast<char *>(OC_VA(0x669758u)))
#define gszErrStatement_g (reinterpret_cast<char *>(OC_VA(0x669858u)))
#else
extern char gszErrMessage_g[0x100] OC_UNALIGNED;
extern char gszErrStatement_g[0x100] OC_UNALIGNED;
#endif

// 0x669958 -- the .pcx resource name err_dlg_init loads the popup art from.
// Each do_err_* writes a literal into it before dlg_load.
#ifdef OC_DLL_MODE
#define gszErrImgResourcePath_g (reinterpret_cast<char *>(OC_VA(0x669958u)))
#else
extern char gszErrImgResourcePath_g[0x104] OC_UNALIGNED;
#endif

// 0x6CE6DC gnDlgReturnVal -- the modal result, a signed long.
#ifdef OC_DLL_MODE
  #ifndef gnDlgReturnVal
  #define gnDlgReturnVal (*reinterpret_cast<LONG *>(OC_VA(0x6CE6DCu)))
  #endif
#else
extern LONG gnDlgReturnVal;
#endif

// 0x66A264 / 0x66A268 / 0x66A26C -- one reentrancy counter per popup shape,
// raised on entry and cleared on every exit. Only a count of 1 opens the
// dialog, so a nested error falls through to the plain popup.
#ifdef OC_DLL_MODE
#define dword_66A264 (*reinterpret_cast<DWORD *>(OC_VA(0x66A264u)))
#define dword_66A268 (*reinterpret_cast<DWORD *>(OC_VA(0x66A268u)))
#define dword_66A26C (*reinterpret_cast<DWORD *>(OC_VA(0x66A26Cu)))
#else
extern DWORD dword_66A264;
extern DWORD dword_66A268;
extern DWORD dword_66A26C;
#endif

// 0x503E5C / 0x503E68 / 0x503E70 -- the three popup event-function tables.
// Their sizes are the literal each body stores into gdwErrPopupTblSize_g:
// 0xC, 8 and 0x14 bytes.
#ifdef OC_DLL_MODE
#define sOKCancelEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503E5Cu)))
#define sOKEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503E68u)))
#define sBigOKEvtFcnTbl_g (reinterpret_cast<PFN_CTRL_EVT *>(OC_VA(0x503E70u)))
#else
extern PFN_CTRL_EVT sOKCancelEvtFcnTbl_g[3] OC_UNALIGNED;
extern PFN_CTRL_EVT sOKEvtFcnTbl_g[2];
extern PFN_CTRL_EVT sBigOKEvtFcnTbl_g[5];
#endif

// The four .rdata resource names, spelled as objects rather than as string
// literals: the oracle copies two of them into gszErrImgResourcePath_g and
// hands the other two to dlg_load, and a literal of ours would land at an
// address we do not control.
#ifndef sgszOkCancelPcx_g
#ifdef OC_DLL_MODE
#define sgszOkCancelPcx_g (reinterpret_cast<char *>(OC_VA(0x4ED7DCu)))
#else
extern char sgszOkCancelPcx_g[17] OC_UNALIGNED;
#endif
#endif
#ifndef sgszOkCancelBin_g
#ifdef OC_DLL_MODE
#define sgszOkCancelBin_g (reinterpret_cast<char *>(OC_VA(0x4ED7F0u)))
#else
extern char sgszOkCancelBin_g[17];
#endif
#endif
#ifndef sgszOkPcx_g
#ifdef OC_DLL_MODE
#define sgszOkPcx_g (reinterpret_cast<char *>(OC_VA(0x4ED804u)))
#else
extern char sgszOkPcx_g[11];
#endif
#endif
#ifndef sgszOkBin_g
#ifdef OC_DLL_MODE
#define sgszOkBin_g (reinterpret_cast<char *>(OC_VA(0x4ED810u)))
#else
extern char sgszOkBin_g[11];
#endif
#endif

// 0x669A60 -- a table of squares, 511 ints centred on 0x669E5C: sub_456B50
// fills it with i*i at both 255-i and 255+i for i in 0..255, and reads it back
// as `dword ptr [<index>*4+669E5Ch]`. The extent is the loop's own bound
// (`cmp ecx, 0FFFFFC00h`) in both directions, and 0x66A260 sits one past it.
#ifdef OC_DLL_MODE
#define dword_669A60 (reinterpret_cast<int *>(OC_VA(0x669A60u)))
#else
extern int dword_669A60[511];
#endif

// 0x66A260 -- the squares table's one-shot init flag.
#ifdef OC_DLL_MODE
#define dword_66A260 (*reinterpret_cast<DWORD *>(OC_VA(0x66A260u)))
#else
extern DWORD dword_66A260;
#endif

// 0x6D3037 gbFontMaxHgt -- the tallest glyph in the current font, a BYTE: the
// only access here is `movzx ecx, byte ptr [6D3037h]`.
#ifdef OC_DLL_MODE
  #ifndef gbFontMaxHgt
  #define gbFontMaxHgt (*reinterpret_cast<BYTE *>(OC_VA(0x6D3037u)))
  #endif
#else
extern BYTE gbFontMaxHgt;
#endif

// 0x4ED7CC -- the TU's own name, the owner tag every SMemAlloc here passes.
#ifdef OC_DLL_MODE
#define sgszOwnerTag_g (reinterpret_cast<char *>(OC_VA(0x4ED7CCu)))
#else
extern char sgszOwnerTag_g[13];
#endif

// The window the dialog loader's measurement callback writes through. dialog.h
// leaves ctrl_t +0x0C unshaped on purpose; err_load_img writes two signed
// 16-bit extents there and, in the slot above them, the decoded bitmap
// dialog.h calls nResIndex.
struct errimg_t {
  /*0x00*/ short nWidth;
  /*0x02*/ short nHeight;
  /*0x04*/ void *pBits;
};

OC_X86_LAYOUT(sizeof(errimg_t) == 0x08, "errimg_t  err_load_img writes +0, +2 and +4");
static_assert(offsetof(errimg_t, nWidth) == 0x00, "movsx edi, word ptr [ebx]");
static_assert(offsetof(errimg_t, nHeight) == 0x02, "lea esi, [ebx+2h] / movsx eax, word ptr [esi]");
OC_X86_LAYOUT(offsetof(errimg_t, pBits) == 0x04, "mov dword ptr [ebx+4h], eax after SMemAlloc");

// A palette entry. sub_456B50 reads the three channels one byte at a time off a
// pointer it advances by four, so the fourth byte is padding it never touches.
struct rgbq_t {
  /*0x00*/ BYTE bRed;
  /*0x01*/ BYTE bGreen;
  /*0x02*/ BYTE bBlue;
  /*0x03*/ BYTE bPad;
};

static_assert(sizeof(rgbq_t) == 0x04, "add edx, 4 walks sgGamePalette_g one entry");
static_assert(offsetof(rgbq_t, bRed) == 0x00, "movzx eax, byte ptr [edx-2h]");
static_assert(offsetof(rgbq_t, bGreen) == 0x01, "movzx ecx, byte ptr [edx-1h]");
static_assert(offsetof(rgbq_t, bBlue) == 0x02, "movzx ecx, byte ptr [edx]");
static_assert(offsetof(rgbq_t, bPad) == 0x03, "never read; the *4 stride is the only witness");

// The bodies, in oracle VA order.

// The dialog loader's binary callback: hand back the named .bin resource,
// copied into the caller's buffer or into one allocated here.
ctrl_t *err_load_binary(void *pCtx, ULONG_PTR dwZero, ULONG_PTR dwZero2,
                        void *pA, dlg_dword_t dwB) {
  void *pBuf = reinterpret_cast<void *>(dwZero);
  DWORD cbRes;
  void *pRes;

  pRes = GetResource(reinterpret_cast<const char *>(pCtx),
                     reinterpret_cast<const char *>(0x1B5C), &cbRes);
  if (pBuf == 0)
    pBuf = SMemAlloc(cbRes, sgszOwnerTag_g, 0x3A, 0);
  memcpy(pBuf, pRes, cbRes);
  if (dwZero2 != 0)
    *reinterpret_cast<DWORD *>(dwZero2) = cbRes;
  return reinterpret_cast<ctrl_t *>(pBuf);
}

// The popup dialog's event handler. Three ids are handled and everything else
// falls through to the stock handler: 0x0A initialises the popup, 7 centres it
// on the 640x480 screen, 1 forgets it.
int err_dlg_fcn(intptr_t nArg0, intptr_t nArg1) {
  ctrl_t *const pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  dlgevt_t *const pEvent = reinterpret_cast<dlgevt_t *>(nArg1);

  if (pEvent->wTag == 0x0E) {
    switch (pEvent->dwId) {
      case 0x0A:
        err_dlg_init(nArg0);
        break;
      case 7:
        // Hyperion: centred in the view's screen, which the menus keep
        // at 640x480 (port_view.cpp); the popup also comes up in a game.
        pCtrl->rc.left = static_cast<short>((g_portView.nWidth - pCtrl->rc.right) / 2);
        pCtrl->rc.top = static_cast<short>((g_portView.nHeight - pCtrl->rc.bottom) / 2);
        break;
      case 1:
        sgpModalPopup = 0;
        break;
      default:
        break;
    }
  }
  return ctrl_dlg(pCtrl, pEvent);
}

// The modal popup deactivates itself with return code 3.
void deactivate_modal_popup() {
  ctrl_t *const pPopup = sgpModalPopup;

  if (pPopup != 0) {
    gnDlgReturnVal = 3;
    dlg_deactivate(pPopup);
  }
}

// Decode the popup art into the dialog's image slot. Inlined, and the helper
// is the lever: its own copy of the dialog pointer is what lets the oracle
// re-form `lea ecx, [esi+0Ch]` for sub_456B50 instead of keeping the view live.
static __forceinline void oc_err_load_art(ctrl_t *pDlg)
{
  BYTE aPalette[0x400];
  DWORD cbRes;
  void *pRes;
  void *pBits;
  errimg_t *pImg = reinterpret_cast<errimg_t *>(&pDlg->_unmapped0C);
  int cbBits;

  pRes = GetResource(gszErrImgResourcePath_g,
                     reinterpret_cast<const char *>(0x1B5D), &cbRes);
  SBmpDecodeImage(2, pRes, cbRes, 0, 0, 0,
                  reinterpret_cast<DWORD_PTR>(&pImg->nWidth),
                  reinterpret_cast<DWORD_PTR>(&pImg->nHeight), 0);
  cbBits = pImg->nWidth * pImg->nHeight;
  pBits = SMemAlloc(cbBits, sgszOwnerTag_g, 0x54, 0);
  pImg->pBits = pBits;
  SBmpDecodeImage(2, pRes, cbRes, aPalette, pBits, cbBits, 0, 0, 0);
  sub_456B50(reinterpret_cast<intptr_t>(pImg), aPalette);
}

// The popup's own initialiser, raised as event 0x0A: send the parent dialog
// away, decode the popup art into the dialog's own image slot, then point the
// message and statement controls at their buffers and pin the two hidden ones.
void err_dlg_init(intptr_t nArg0) {
  ctrl_t *const pDlg = reinterpret_cast<ctrl_t *>(nArg0);
  ctrl_t *pCtrl;

  if (gpDlgList != 0) {
    ctrl_send_msg(gpDlgList, 6, 0);
    pDlg->dwFlags |= 0x8000000;
  }
  oc_err_load_art(pDlg);

  if (gErrPopupFcnTbl_g != 0)
    ctrl_set_evt_fcns(pDlg, gErrPopupFcnTbl_g, gdwErrPopupTblSize_g);

  pCtrl = ctrl_find_ndx(pDlg, 2);
  if (pCtrl != 0) {
    pCtrl->pData = gszErrMessage_g;
    pCtrl->pfnDraw = popup_txt_update_fcn;
    sub_4572A0(reinterpret_cast<intptr_t>(pCtrl));
  }
  if (gszErrStatement_g[0] != 0) {
    pCtrl = ctrl_find_ndx(pDlg, 5);
    if (pCtrl != 0) {
      pCtrl->pData = gszErrStatement_g;
      sub_4572A0(reinterpret_cast<intptr_t>(pCtrl));
    }
  }
  ctrl_find_ndx(pDlg, -2)->dwFlags |= 0x20000;
  pCtrl = ctrl_find_ndx(pDlg, -3);
  if (pCtrl != 0)
    pCtrl->dwFlags |= 0x20000;
}

// The dialog loader's measurement callback: decode the named .pcx once for its
// extents, allocate the bitmap and decode it again into that buffer.
void err_load_img(intptr_t nArg0, intptr_t nArg1, intptr_t nArg2, int nArg3, int nArg4) {
  errimg_t *const pImg = reinterpret_cast<errimg_t *>(nArg1);
  DWORD cbRes;
  void *pRes;
  int cbBits;

  pRes = GetResource(reinterpret_cast<const char *>(nArg0),
                     reinterpret_cast<const char *>(0x1B5D), &cbRes);
  SBmpDecodeImage(2, pRes, cbRes, 0, 0, 0,
                  reinterpret_cast<DWORD_PTR>(&pImg->nWidth),
                  reinterpret_cast<DWORD_PTR>(&pImg->nHeight), 0);
  cbBits = pImg->nWidth * pImg->nHeight;
  pImg->pBits = SMemAlloc(cbBits, sgszOwnerTag_g, 0x54, 0);
  SBmpDecodeImage(2, pRes, cbRes, reinterpret_cast<void *>(nArg2), pImg->pBits,
                  cbBits, 0, 0, 0);
}

// The OK/Cancel error popup. A nested call falls through to the plain
// text-only popup; otherwise load the art, run the dialog modally and report
// whether the user took OK.
BYTE do_err_okcancel_dlg(const char *pszText) {
  ctrl_t *pDlg;
  int nResult;
  int nOk;
  BYTE bOk;

  deactivate_modal_popup();
  if (gpDlgList != 0 && ++dword_66A264 == 1) {
    nOk = do_pok_cancel_dlg(reinterpret_cast<intptr_t>(pszText));
    dword_66A264 = 0;
    return static_cast<BYTE>(nOk);
  }
  gErrPopupFcnTbl_g = sOKCancelEvtFcnTbl_g;
  gdwErrPopupTblSize_g = 0x0C;
  strcpy(gszErrMessage_g, pszText);
  memcpy(gszErrImgResourcePath_g, sgszOkCancelPcx_g, 17);
  pDlg = dlg_load(sgszOkCancelBin_g,
                  reinterpret_cast<PFN_DLGLOADER>(err_load_binary),
                  reinterpret_cast<PFN_DLGMEASURE>(err_load_img),
                  sgszOwnerTag_g, 0x11E);
  sgpModalPopup = pDlg;
  nResult = dlg_run_modal(pDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(err_dlg_fcn));
  if (nResult == -3 || nResult == 3)
    bOk = 0;
  else
    bOk = 1;
  dword_66A264 = 0;
  return bOk;
}

// 0x4570D0 (216 B). The plain OK error popup, and the fallback
// do_err_ok_statement_dlg tail-jumps to when there is no statement to show.
BYTE sub_4570D0(const char *pszText) {
  ctrl_t *pDlg;
  int nOk;

  deactivate_modal_popup();
  if (gpDlgList != 0 && ++dword_66A268 == 1) {
    nOk = do_pok_dlg(reinterpret_cast<intptr_t>(pszText));
    dword_66A268 = 0;
    return static_cast<BYTE>(nOk);
  }
  gErrPopupFcnTbl_g = sOKEvtFcnTbl_g;
  gdwErrPopupTblSize_g = 8;
  strcpy(gszErrMessage_g, pszText);
  memcpy(gszErrImgResourcePath_g, sgszOkPcx_g, 11);
  pDlg = dlg_load(sgszOkBin_g,
                  reinterpret_cast<PFN_DLGLOADER>(err_load_binary),
                  reinterpret_cast<PFN_DLGMEASURE>(err_load_img),
                  sgszOwnerTag_g, 0x142);
  sgpModalPopup = pDlg;
  dlg_run_modal(pDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(err_dlg_fcn));
  dword_66A268 = 0;
  return 1;
}

// 0x456FB0 (285 B). The OK popup with a second line of statement text. An
// empty statement makes it the plain popup, and a nested call the split popup.
BYTE do_err_ok_statement_dlg(const char *pszText, const char *pszStatement) {
  ctrl_t *pDlg;
  int bOk;

  deactivate_modal_popup();
  if (pszStatement == 0 || *pszStatement == 0)
    return sub_4570D0(pszText);

  bOk = 1;
  if (gpDlgList != 0 && ++dword_66A26C == 1) {
    bOk = do_pok_split_dlg(reinterpret_cast<intptr_t>(pszText),
                           reinterpret_cast<intptr_t>(pszStatement));
  } else {
    gErrPopupFcnTbl_g = sBigOKEvtFcnTbl_g;
    gdwErrPopupTblSize_g = 0x14;
    strcpy(gszErrMessage_g, pszText);
    strcpy(gszErrStatement_g, pszStatement);
    memcpy(gszErrImgResourcePath_g, sgszOkPcx_g, 11);
    pDlg = dlg_load(sgszOkBin_g,
                    reinterpret_cast<PFN_DLGLOADER>(err_load_binary),
                    reinterpret_cast<PFN_DLGMEASURE>(err_load_img),
                    sgszOwnerTag_g, 0x167);
    sgpModalPopup = pDlg;
    dlg_run_modal(pDlg, 0, reinterpret_cast<PFN_CTRL_EVT>(err_dlg_fcn));
  }
  dword_66A26C = 0;
  return static_cast<BYTE>(bOk);
}

// Shrink a text control until its text fits: while the line count the current
// font needs exceeds the rows the control has, rotate the horizontal alignment
// bits and measure again.
void sub_4572A0(intptr_t nArg0) {
  ctrl_t *const pCtrl = reinterpret_cast<ctrl_t *>(nArg0);
  int cx;
  int cy;
  int nRows;
  DWORD dwAlign;

  if (pCtrl->pData == 0)
    return;
  dlg_set_font(pCtrl);
  cx = pCtrl->rc.right - pCtrl->rc.left;
  cy = pCtrl->rc.bottom - pCtrl->rc.top;
  ++cx;
  ++cy;
  if (cx == 0)
    cx = 1;
  nRows = cy * 10;
  while (font_strlen(reinterpret_cast<const char *>(pCtrl->pData)) * 10 / cx +
             10 >=
         nRows / (gbFontMaxHgt + 1)) {
    dwAlign = pCtrl->dwFlags & 0x4C00;
    pCtrl->dwFlags &= ~dwAlign;
    if (dwAlign == 0x400) {
      pCtrl->dwFlags |= 0x400;
    } else if (dwAlign == 0x800) {
      /* the bit is dropped, not rotated */
    } else if (dwAlign == 0x4000) {
      pCtrl->dwFlags |= 0x800;
    } else {
      pCtrl->dwFlags |= 0x400;
    }
    dlg_set_font(pCtrl);
  }
  font_set(0);
}

// Nearest game-palette entry to one source colour by summed squared channel
// distance, stopping at an exact hit. It must be an inlined helper returning
// BYTE: the early return keeps the best index out of a callee-saved register,
// and the named channel differences give the palette walk its +2 bias.
static __forceinline BYTE oc_nearest_color(const rgbq_t *pSrc)
{
  const rgbq_t *pGame = reinterpret_cast<const rgbq_t *>(sgGamePalette_g);
  unsigned int nBest = 0xFFFFFFFFu;
  int nBestIndex = 0;
  int i;

  for (i = 0; i < 256; i++, pGame++) {
    int nR = pGame->bRed - pSrc->bRed;
    int nG = pGame->bGreen - pSrc->bGreen;
    int nB = pGame->bBlue - pSrc->bBlue;
    unsigned int nDist = dword_669A60[255 + nR] + dword_669A60[255 + nG] +
                         dword_669A60[255 + nB];
    if (nDist == 0)
      return i;
    if (nDist < nBest) {
      nBest = nDist;
      nBestIndex = i;
    }
  }
  return nBestIndex;
}

// Remap an image's pixels from the palette the caller decoded it with onto the
// game palette. The table of squares is built once on the first call,
// vectorised by the compiler behind an __isa_available test.
void sub_456B50(intptr_t nArg0, void *pArg1) {
  errimg_t *const pImg = reinterpret_cast<errimg_t *>(nArg0);
  BYTE *pBits;
  int nPixels;
  int i;

  if (dword_66A260 == 0) {
    for (i = 0; i < 256; i++) {
      dword_669A60[255 - i] = i * i;
      dword_669A60[255 + i] = i * i;
    }
    dword_66A260 = 1;
  }

  nPixels = pImg->nHeight * pImg->nWidth;
  pBits = static_cast<BYTE *>(pImg->pBits);
  while (nPixels-- != 0) {
    const rgbq_t *pSrc = &static_cast<const rgbq_t *>(pArg1)[*pBits];
    *pBits++ = oc_nearest_color(pSrc);
  }
}
