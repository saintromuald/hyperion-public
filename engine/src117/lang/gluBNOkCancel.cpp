// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNOkCancel.cpp -- the battle.net OK / OK-Cancel message box, a real Win32
// dialog drawn over the glue. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBNOkCancel.h"
#include "../gen/globals117.h"

#include "../h/gluBN.h"          // LoadArtFile, SDrawGetFrameWindow, blt_size_t
#include "../h/gluBNProfile.h"   // SDlg*, profile_logfont_t
#include "../h/glues.h"          // sub_49E740
#include "../h/Storm.h"          // SMemFree, and the one <windows.h>

#include <string.h>

// SelectObject, the one GDI import gluBNProfile.h does not declare: Storm.h
// defines NOGDI before <windows.h>. The USER32 and KERNEL32 names come from
// <windows.h> and must not be restated (C2733).
extern "C" __declspec(dllimport) void *__stdcall SelectObject(void *hdc, void *h);

// One font row: the point size MulDiv scales and the face name. The three rows
// are 8/"Arial", 10/"Arial" and 13/"Times New Roman".
struct okfont_t {
  /*0x00*/ int nPointSize;
  /*0x04*/ const char *pszFace;
};

OC_X86_LAYOUT(sizeof(okfont_t) == 0x08, "okfont_t stride  [edi*8+503550h]");
static_assert(offsetof(okfont_t, nPointSize) == 0x00, "okfont_t::nPointSize  push dword ptr [edi*8+503550h] into MulDiv");
OC_X86_LAYOUT(offsetof(okfont_t, pszFace)    == 0x04, "okfont_t::pszFace     mov ecx, dword ptr [edi*8+503554h]");

// The globals this TU reaches, dual-spelled. The two artwork arrays hold the
// small and the large popup each: sgSize picks the element and sgErrorFlag the
// array. sgOkCtrlList_g has at most four entries; a string begins at 0x503588.
#ifdef OC_DLL_MODE
#define sgOkFonts_g (reinterpret_cast<okfont_t *>(OC_VA(0x503550u)))
#define sgOkDlgArt_g (reinterpret_cast<const char **>(OC_VA(0x503568u)))
#define sgOkCancelDlgArt_g (reinterpret_cast<const char **>(OC_VA(0x503570u)))
#define sgOkCtrlList_g (reinterpret_cast<int *>(OC_VA(0x503578u)))
#else
extern okfont_t sgOkFonts_g[3];
extern const char *sgOkDlgArt_g[2];
extern const char *sgOkCancelDlgArt_g[2];
extern int sgOkCtrlList_g[4];
#endif

// The box's live state: sgBgBmp and sgBtnBmp are the two loaded bitmaps and
// sgTitle the caption; sgTestSize, sgSize and sgErrorFlag are flags;
// sghCurrentBox is the box up now and dword_667B24 the font for WM_SETFONT.
#ifdef OC_DLL_MODE
#define sgBgBmp (*reinterpret_cast<void **>(OC_VA(0x667B08u)))
#define sgBtnBmp (*reinterpret_cast<void **>(OC_VA(0x667B0Cu)))
#define sgTestSize (*reinterpret_cast<int *>(OC_VA(0x667B10u)))
#define sgTitle (*reinterpret_cast<const char **>(OC_VA(0x667B14u)))
#define sgSize (*reinterpret_cast<int *>(OC_VA(0x667B18u)))
#define sgErrorFlag (*reinterpret_cast<int *>(OC_VA(0x667B1Cu)))
#define sghCurrentBox (*reinterpret_cast<void **>(OC_VA(0x667B20u)))
#define dword_667B24 (*reinterpret_cast<void **>(OC_VA(0x667B24u)))
#else
extern void *sgBgBmp;
extern void *sgBtnBmp;
extern int sgTestSize;
extern const char *sgTitle;
extern int sgSize;
extern int sgErrorFlag;
extern void *sghCurrentBox;
extern void *dword_667B24;
#endif

// 0x5034D0 sgOkDlgs and 0x503510 sgOkCancelDlgs, the sheet's names: each is
// the small and the large dialog template name, 0x20 bytes apiece
// ("OK_DIALOG", "BIGOK_DIALOG", "OKCANCEL_DIALOG", "BIGOKCANCEL_DIALOG").
#ifdef OC_DLL_MODE
#define sgOkDlgs (reinterpret_cast<char (*)[0x20]>(OC_VA(0x5034D0u)))
#define sgOkCancelDlgs (reinterpret_cast<char (*)[0x20]>(OC_VA(0x503510u)))
#else
extern char sgOkDlgs[2][0x20];
extern char sgOkCancelDlgs[2][0x20];
#endif

// The bodies, in oracle VA order.

// 0x44E110. Storm's message-box callback: close any box already up, then run
// the OK or OK-Cancel dialog, first at the small and the large size, then at
// the large size with ever smaller fonts. When nothing fits, fall back to the
// system MessageBoxA.
int __stdcall UiMessageBoxCallback(void *hDlg, const char *pszText,
                                   const char *pszCaption, int nFlags)
{
  char (*pDlgs)[0x20];
  int nResult;

  if (sghCurrentBox != NULL) {
    SDlgEndDialog(sghCurrentBox, 0);
    SendMessageA(static_cast<HWND>(sghCurrentBox), WM_DESTROY, 0, 0);
  }
  sgTitle = pszCaption;
  pDlgs = (nFlags & 0xF) == 0 ? sgOkDlgs : sgOkCancelDlgs;
  switch (nFlags & 0xF0) {
  case 0x10:
  case 0x30:
    sgErrorFlag = 1;
    break;
  default:
    sgErrorFlag = 0;
    break;
  }
  ShowCursor(TRUE);
  sgTestSize = 1;
  dword_667B24 = OkCancelFontCreate(hDlg, 2);
  for (sgSize = 0; sgSize < 2; sgSize++) {
    nResult = gluBN_DoModalDlg(pDlgs[sgSize], hDlg,
                               reinterpret_cast<void *>(OkCancelDialogProc),
                               reinterpret_cast<intptr_t>(pszText));
    if (nResult != (int)0xFF000000)
      goto done;
  }
  for (int nFont = 1; nFont >= 0; nFont--) {
    DeleteObject(dword_667B24);
    dword_667B24 = OkCancelFontCreate(hDlg, nFont);
    nResult = gluBN_DoModalDlg(pDlgs[1], hDlg,
                               reinterpret_cast<void *>(OkCancelDialogProc),
                               reinterpret_cast<intptr_t>(pszText));
    if (nResult != (int)0xFF000000)
      break;
  }
done:
  ShowCursor(FALSE);
  if (dword_667B24 != NULL) {
    DeleteObject(dword_667B24);
    dword_667B24 = NULL;
  }
  if (nResult == (int)0xFF000000)
    return MessageBoxA(static_cast<HWND>(hDlg), pszText, pszCaption, nFlags);
  return nResult;
}

// 0x44E270. True when the text still fits the control. A missing window, a
// missing string or an empty one all answer "fits" before any GDI call, and a
// failed GetDC or a control with no font answer "does not".
int IsTextTooLarge(void *hWnd, const char *pszText)
{
  RECT rcClient;
  RECT rcText;
  HDC hdc;
  void *hFont;
  void *hOld;

  if (hWnd == NULL)
    return 1;
  if (pszText == NULL)
    return 1;
  if (*pszText == '\0')
    return 1;

  GetClientRect(static_cast<HWND>(hWnd), &rcClient);
  rcText = rcClient;
  hdc = GetDC(static_cast<HWND>(hWnd));
  if (hdc == NULL)
    return 0;

  hFont = reinterpret_cast<void *>(
      SendMessageA(static_cast<HWND>(hWnd), 0x31, 0, 0));
  if (hFont == NULL)
    return 0;

  hOld = SelectObject(hdc, hFont);
  DrawTextA(hdc, pszText, (int)strlen(pszText), &rcText, 0x410);
  if (hOld != NULL)
    SelectObject(hdc, hOld);
  ReleaseDC(static_cast<HWND>(hWnd), hdc);

  if (rcText.bottom < rcClient.bottom && rcText.right <= rcClient.right)
    return 1;
  return 0;
}

// 0x44E330. Releases the background and the button artwork. The owner lines
// 0x59 and 0x5D are 1.17's, not __LINE__.
void OkCancelDestroy(void *hDlg)
{
  if (sgBgBmp != NULL) {
    SMemFree(sgBgBmp, "gluBNOkCancel.cpp", 0x59, 0);
    sgBgBmp = NULL;
  }
  if (sgBtnBmp != NULL) {
    SMemFree(sgBtnBmp, "gluBNOkCancel.cpp", 0x5D, 0);
    sgBtnBmp = NULL;
  }
}

// 0x44E380. The box's dialog procedure. Alt keys are forwarded to the game's
// own frame window, and the three command ids all pass through sub_49E740(0xA7)
// -- the click sound -- before ending the dialog.
intptr_t __stdcall OkCancelDialogProc(void *hDlg, unsigned int nMsg,
                                 uintptr_t wParam, LONG_PTR lParam)
{
  switch (nMsg) {
  case 0x104:
  case 0x105:
    SendMessageA(static_cast<HWND>(SDrawGetFrameWindow(0)), nMsg, wParam,
                 lParam);
    break;
  case 2:
    OkCancelDestroy(hDlg);
    sghCurrentBox = NULL;
    break;
  case 0x110:
    if (!OkCancelInit(hDlg, reinterpret_cast<const char *>(lParam)))
      SDlgEndDialog(hDlg, -0x1000000);
    sghCurrentBox = hDlg;
    return 1;
  case 0x138:
    if (GetWindowLongA(reinterpret_cast<HWND>(lParam), -12) != 0x40E)
      break;
    SetTextColor(reinterpret_cast<void *>(wParam), 0xFFFF);
    return reinterpret_cast<intptr_t>(GetStockObject(5));
  case 0x111:
    switch (static_cast<unsigned short>(wParam)) {
    case 0x455:
      sub_49E740(0xA7);
      SDlgEndDialog(hDlg, 1);
      break;
    case 2:
      sub_49E740(0xA7);
      SDlgEndDialog(hDlg, 2);
      break;
    case 1:
      sub_49E740(0xA7);
      if (GetWindowLongA(GetFocus(), -12) != 0x455)
        SDlgEndDialog(hDlg, 2);
      else
        SDlgEndDialog(hDlg, 1);
      break;
    }
    break;
  }
  return SDlgDefDialogProc(hDlg, nMsg, wParam, lParam);
}

// 0x44E4D0. Clones the window's current font at one of sgOkFonts_g's sizes, in
// bold. 96 and 72 are 1.17's own literals, not GetDeviceCaps.
void *OkCancelFontCreate(void *hWnd, int nIndex)
{
  profile_logfont_t lf;
  void *hFont;

  if (hWnd == NULL) {
    hWnd = GetActiveWindow();
    if (hWnd == NULL)
      return NULL;
  }

  hFont = reinterpret_cast<void *>(
      SendMessageA(static_cast<HWND>(hWnd), 0x31, 0, 0));
  if (hFont == NULL)
    return NULL;

  memset(&lf, 0, sizeof(lf));
  if (!GetObjectA(hFont, sizeof(lf), &lf))
    return NULL;

  lf.lfHeight = -MulDiv(sgOkFonts_g[nIndex].nPointSize, 96, 72);
  lf.lfWeight = 700;
  strcpy(lf.lfFaceName, sgOkFonts_g[nIndex].pszFace);
  return CreateFontIndirectA(&lf);
}

// 0x44E580. WM_INITDIALOG. sgSize picks the large or the small popup and
// sgErrorFlag picks the OK-Cancel artwork over the OK artwork; when the dialog's
// parent is not the game's frame window the art is loaded under "Popup" against
// that parent instead.
int OkCancelInit(void *hDlg, const char *pszText)
{
  blt_size_t size;
  const char *pszArt;
  HWND hItem;

  if (sgSize == 0)
    pszArt = sgErrorFlag ? sgOkCancelDlgArt_g[0] : sgOkDlgArt_g[0];
  else
    pszArt = sgErrorFlag ? sgOkCancelDlgArt_g[1] : sgOkDlgArt_g[1];

  if (SDrawGetFrameWindow(0) == GetParent(static_cast<HWND>(hDlg)))
    LoadArtFile(hDlg, NULL, "", -1, 1, pszArt, &sgBgBmp, NULL, 1);
  else
    LoadArtFile(hDlg, GetParent(static_cast<HWND>(hDlg)), "Popup", -1, 1,
                pszArt, &sgBgBmp, NULL, 1);

  LoadArtFile(NULL, NULL, "Popup", -1, 1,
              "glue\\battle.net\\generic\\icons\\but_sml.pcx", &sgBtnBmp, &size,
              1);
  SDlgSetControlBitmaps(hDlg, sgOkCtrlList_g, 0, sgBtnBmp, &size, 1, -1);

  hItem = GetDlgItem(static_cast<HWND>(hDlg), 0x402);
  if (dword_667B24 != NULL)
    SendMessageA(hItem, 0x30, reinterpret_cast<WPARAM>(dword_667B24), 0);
  SetWindowTextA(hItem, pszText);
  if (sgTestSize && !IsTextTooLarge(hItem, pszText))
    return 0;

  if (sgTitle != NULL) {
    hItem = GetDlgItem(static_cast<HWND>(hDlg), 0x40E);
    if (sgTestSize && !IsTextTooLarge(hItem, sgTitle))
      return 0;
    if (hItem != NULL)
      SetWindowTextA(hItem, sgTitle);
  }
  return 1;
}
