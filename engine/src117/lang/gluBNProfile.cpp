// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNProfile.cpp -- the battle.net profile dialog: its panel painters, link
// handling, fonts and the profile key tables. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include <stdlib.h>            // strtoul -- the last-game timestamp arrives as
                               // two decimal halves of a FILETIME
#include <stdio.h>             // fopen, fclose -- sub_44F090 touches the file
                               // only to make sure it exists
#include <string.h>            // memset -- UiProfileBitmapCallback clears the
                               // field-value table before refilling it

#include "../h/Storm.h"       // the tree's only <windows.h>, behind NOGDI; the
                            // header spells handles void *
#include "../h/gluBNProfile.h"
#include "../h/opt_dlgs.h"   // _stricmp
#include "../h/glues.h"      // glues_get_str
#include "../h/victory.h"    // psnprintf
#include "../gen/globals117.h"

// At C linkage, textually identical to the copy in init.cpp.
extern "C" {
int __cdecl _strnicmp(const char *psz1, const char *psz2, size_t cch);
}

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x536720 -- the expansion (Brood War) gate, a BYTE compared against zero.
#ifdef OC_DLL_MODE
#define gbIsExpansion (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE gbIsExpansion;
#endif

// 0x503588 and 0x503718 -- the two packed, double-NUL-terminated profile key
// lists UiGetProfileFields chooses between, the Star one and the SEXP one.
#ifdef OC_DLL_MODE
#define sgStarProfileKeys_g (reinterpret_cast<char *>(OC_VA(0x503588u)))
#define sgSexpProfileKeys_g (reinterpret_cast<char *>(OC_VA(0x503718u)))
#else
extern char sgStarProfileKeys_g[];
extern char sgSexpProfileKeys_g[];
#endif

// 0x667B90 and 0x667B94 -- the profile bitmap and the blank plate, allocated in
// bn_profile_init and freed in bn_profile_free.
#ifdef OC_DLL_MODE
#define sgpProfileBmp      (*reinterpret_cast<void **>(OC_VA(0x667B90u)))
#define sgpBlankProfileBmp (*reinterpret_cast<void **>(OC_VA(0x667B94u)))
#else
extern void *sgpProfileBmp;
extern void *sgpBlankProfileBmp;
#endif

// 0x4ECF58 and 0x4ED078 -- the Star and SEXP profile key tables, 18 rows each.
#ifdef OC_DLL_MODE
#define sgStarProfileKeyTable (reinterpret_cast<const profile_key_t *>(OC_VA(0x4ECF58u)))
#define sgSexpProfileKeyTable (reinterpret_cast<const profile_key_t *>(OC_VA(0x4ED078u)))
#else
extern const profile_key_t sgStarProfileKeyTable[18];
extern const profile_key_t sgSexpProfileKeyTable[18];
#endif

// 0x667B28 -- the 18 field values sub_44FB40 fills in.
#ifdef OC_DLL_MODE
#define sgProfileValues (reinterpret_cast<const char **>(OC_VA(0x667B28u)))
#else
extern const char *sgProfileValues[18];
#endif

// 0x4ED520 -- the "0" that stands in for an empty field value.
#ifdef OC_DLL_MODE
#define szProfileZero (reinterpret_cast<const char *>(OC_VA(0x4ED520u)))
#else
extern const char szProfileZero[];
#endif

// 0x5038A4 -- the font table UiGetProfileFont indexes; its row count is not
// known.
#ifdef OC_DLL_MODE
#define sgProfileFonts (reinterpret_cast<const profile_font_t *>(OC_VA(0x5038A4u)))
#else
extern const profile_font_t sgProfileFonts[];
#endif

// 0x667B98 and 0x667B9C -- the profile bitmap's width and height.
#ifdef OC_DLL_MODE
#define sgProfileBmpWidth  (*reinterpret_cast<int *>(OC_VA(0x667B98u)))
#define sgProfileBmpHeight (*reinterpret_cast<int *>(OC_VA(0x667B9Cu)))
#else
extern int sgProfileBmpWidth;
extern int sgProfileBmpHeight;
#endif

// The six slots UiProfileCallback fills from its arguments: the field count,
// the key and value arrays, the save hook, the caption, and a flag that picks
// the league standing over the last-game line.
#ifdef OC_DLL_MODE
#define oc_d667B70        (*reinterpret_cast<int *>(OC_VA(0x667B70u)))
#define oc_d667B7C        (*reinterpret_cast<char **>(OC_VA(0x667B7Cu)))
#define sgcProfileFields  (*reinterpret_cast<int *>(OC_VA(0x667B80u)))
#define sgppProfileKeys   (*reinterpret_cast<char ***>(OC_VA(0x667B84u)))
#define sgppProfileValues (*reinterpret_cast<char ***>(OC_VA(0x667B88u)))
#define sgpfnProfileSave  (*reinterpret_cast<PFN_PROFILE_SAVE *>(OC_VA(0x667B8Cu)))
#else
extern int              oc_d667B70;
extern char            *oc_d667B7C;
extern int              sgcProfileFields;
extern char           **sgppProfileKeys;
extern char           **sgppProfileValues;
extern PFN_PROFILE_SAVE sgpfnProfileSave;
#endif

// The profile dialog's five owned resources: the two art files sub_44EFF0
// loads, the two fonts from sub_44FA60, and the picture box's bitmap.
#ifdef OC_DLL_MODE
#define sgpProfileArt      (*reinterpret_cast<void **>(OC_VA(0x667B74u)))
#define sgpProfileCtrlArt  (*reinterpret_cast<void **>(OC_VA(0x667B78u)))
#define sghProfileFont1    (*reinterpret_cast<void **>(OC_VA(0x667BA0u)))
#define sghProfileFont2    (*reinterpret_cast<void **>(OC_VA(0x667BA4u)))
#define sgpProfileDescBuf  (*reinterpret_cast<void **>(OC_VA(0x667BA8u)))
#else
extern void *sgpProfileArt;
extern void *sgpProfileCtrlArt;
extern void *sghProfileFont1;
extern void *sghProfileFont2;
extern void *sgpProfileDescBuf;
#endif

// 0x4ED5C4 and 0x4ED5DC -- the two zero-terminated control-id lists sub_44F4C0
// hands to sub_44FA60.
#ifdef OC_DLL_MODE
#define sgProfileIconCtrlIds (reinterpret_cast<const int *>(OC_VA(0x4ED5C4u)))
#define sgProfileTextCtrlIds (reinterpret_cast<const int *>(OC_VA(0x4ED5DCu)))
#else
extern const int sgProfileIconCtrlIds[];
extern const int sgProfileTextCtrlIds[];
#endif


// The bodies, in oracle VA order.

// Hit-tests the cursor against the static control's text and follows whatever
// link is under it. The cursor is moved into the control's own coordinates
// before the text is measured.
void sub_44E6E0(void *hDlgArg, void *hwndItemArg)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  HWND hwndItem = static_cast<HWND>(hwndItemArg);
  HDC hdcMem;
  char *pszUrl;
  HDC hdcDesktop;
  POINT pt;
  RECT rcWindow;
  RECT rcClient;
  char *pszText;
  intptr_t cch;

  if (GetCursorPos(&pt) == 0)
    return;
  if (GetWindowRect(hwndItem, &rcWindow) == 0)
    return;
  if (GetClientRect(hwndItem, &rcClient) == 0)
    return;
  pt.x -= rcWindow.left;
  pt.y -= rcWindow.top;
  hdcDesktop = GetDC(GetDesktopWindow());
  hdcMem = static_cast<HDC>(CreateCompatibleDC(hdcDesktop));
  cch = SendMessageA(hwndItem, WM_GETTEXTLENGTH, 0, 0) + 1;
  if (cch > 1) {
    pszText = static_cast<char *>(SMemAlloc((DWORD)cch, "gluBNProfile.cpp", 0x3C8, 0));
    SendMessageA(hwndItem, WM_GETTEXT, cch,
                 reinterpret_cast<LPARAM>(pszText));
    if (sub_44E9A0(hdcMem, reinterpret_cast<rect32_t *>(&rcClient), pszText,
                   &pt, &pszUrl))
      sub_44F090(hDlg, pszUrl);
    SMemFree(pszText, "gluBNProfile.cpp", 0x3CE, 0);
  }
  DeleteDC(hdcMem);
  ReleaseDC(GetDesktopWindow(), hdcDesktop);
}


// Stretch-blits the loaded profile bitmap onto a surface. Storm wants a rect
// and a size for each side, so four temporaries are filled first.
void bn_blit_profile_img(void *pSurface, void *pBits, int nWidth, int nHeight)
{
  blt_size_t srcSize;
  blt_size_t dstSize;
  blt_rect_t srcRect;
  blt_rect_t dstRect;

  if (pSurface == 0 || pBits == 0)
    return;
  dstRect.nLeft = 0;
  dstRect.nTop = 0;
  dstRect.nRight = nWidth;
  dstRect.nBottom = nHeight;
  dstSize.cx = nWidth;
  dstSize.cy = nHeight;
  srcRect.nLeft = 0;
  srcRect.nTop = 0;
  srcRect.nRight = sgProfileBmpWidth;
  srcRect.nBottom = sgProfileBmpHeight;
  srcSize.cx = sgProfileBmpWidth;
  srcSize.cy = sgProfileBmpHeight;
  SBltROP3Clipped(reinterpret_cast<DWORD_PTR>(pSurface),
                  reinterpret_cast<DWORD_PTR>(&dstRect),
                  reinterpret_cast<DWORD_PTR>(&dstSize), nWidth,
                  reinterpret_cast<DWORD_PTR>(pBits), &srcRect, &srcSize,
                  sgProfileBmpWidth, 0, 0xCC0020);
}


// Draws the "no profile" plate: the blank bitmap, then one centred line of text
// from the string table. The x is centred in a fixed 0xEA-wide box, not in the
// surface the caller passes.
void UiProfileDrawBlank(void *pSurface, int nWidth, int nHeight)
{
  blt_rect_t rc;
  blt_size_t ext;
  void *pFont;
  const char *pszText;

  bn_blit_profile_img(pSurface, sgpBlankProfileBmp, nWidth, nHeight);
  pFont = UiGetProfileFont(1);
  if (pFont == 0)
    return;
  SGdiSelectObject(pFont);
  rc.nLeft = 0;
  rc.nTop = 0;
  rc.nRight = nWidth;
  rc.nBottom = nHeight;
  pszText = glues_get_str(0xA4);
  SGdiGetTextExtent(pszText, static_cast<int>(strlen(pszText)), &ext);
  SGdiSetPitch(nWidth);
  SGdiExtTextOut(pSurface, (0xEA - ext.cx) / 2 + 0x10, 0x42, &rc, 0x10000FF, 1,
                 0, pszText, -1);
  SGdiDeleteObject(pFont);
}


// Owner-draws the profile's static text control. The text is pulled out of the
// control into a temporary buffer, painted, and released.
void sub_44E930(void *hDlgArg, void *pDrawItem)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  DRAWITEMSTRUCT *pDraw = static_cast<DRAWITEMSTRUCT *>(pDrawItem);
  char *pszText;
  intptr_t cch;

  if (pDraw->hDC == 0)
    return;
  if (pDraw->CtlType != ODT_STATIC)
    return;
  cch = SendMessageA(pDraw->hwndItem, WM_GETTEXTLENGTH, 0, 0) + 1;
  if (cch == 1)
    return;
  pszText = static_cast<char *>(SMemAlloc((DWORD)cch, "gluBNProfile.cpp", 0x36C, 0));
  SendMessageA(pDraw->hwndItem, WM_GETTEXT, cch,
               reinterpret_cast<LPARAM>(pszText));
  sub_44E9A0(pDraw->hDC, reinterpret_cast<rect32_t *>(&pDraw->rcItem),
             pszText, 0, 0);
  SMemFree(pszText, "gluBNProfile.cpp", 0x371, 0);
}


// Lays the control's text out as alternating plain and link runs, and reports
// whether the cursor landed on a link. Each run is terminated in place and the
// byte put back before the next one.
BOOL sub_44E9A0(void *hDCArg, rect32_t *prcItem, char *pszText,
                const void *pPtArg, char **ppEnd)
{
  HDC hDC = static_cast<HDC>(hDCArg);
  const POINT *pPt = static_cast<const POINT *>(pPtArg);
  rect32_t rcHit;
  _DRAWTEXT *pDraw;
  rect32_t *prcHit;
  char *pszUrl;
  char *pszStop;
  char chSaved;

  pDraw = DrawTextCreate(hDC, reinterpret_cast<const rect32_t *>(prcItem));
  if (pDraw == 0)
    return FALSE;
  if (pszText != 0) {
    prcHit = pPt != 0 ? &rcHit : 0;
    for (;;) {
      pszUrl = _mbsstr(pszText, "http://");
      chSaved = '\0';
      if (pszUrl != 0) {
        chSaved = *pszUrl;
        *pszUrl = '\0';
      }
      if (sub_449D30(pDraw, pszText, 1, prcHit) == 0)
        break;
      if (pszUrl == 0)
        break;
      *pszUrl = chSaved;
      pszStop = _mbspbrk(pszUrl, " \n\r\t");
      if (pszStop != 0) {
        chSaved = *pszStop;
        *pszStop = '\0';
      }
      if (sub_449D30(pDraw, pszUrl, 2, prcHit) == 0)
        break;
      if (pPt != 0 && PtInRect(reinterpret_cast<const RECT *>(&rcHit), *pPt)) {
        if (ppEnd != 0)
          *ppEnd = pszUrl;
        return TRUE;
      }
      if (pszStop == 0)
        break;
      *pszStop = chSaved;
      pszText = pszStop;
    }
  }
  RemoveBNText(pDraw);
  return pPt == 0;
}


// Draws the profile panel's win/loss block and the last-game line. The last
// line is either a formatted local timestamp or the league standing, depending
// on which of the two the caller has set up.
void sub_44EAA0(void *pSurface, int nWidth, int nHeight)
{
  char szLine[0x800];
  RECT rc;
  blt_size_t ext;
  void *hFontSmall;
  void *hFontBig;
  const char *pszWins;
  const char *pszLosses;
  const char *pszDisconnects;
  const char *pszRating;
  const char *psz;
  int cch;

  hFontSmall = UiGetProfileFont(1);
  hFontBig = UiGetProfileFont(3);
  if (hFontSmall == 0)
    return;
  if (hFontBig == 0)
    return;
  rc.left = 0;
  rc.top = 0;
  rc.right = nWidth;
  rc.bottom = nHeight;
  pszWins = sgProfileValues[4];
  pszLosses = sgProfileValues[5];
  pszDisconnects = sgProfileValues[6];
  pszRating = sgProfileValues[12];
  SGdiSetPitch(nWidth);
  SGdiSelectObject(hFontSmall);
  psz = glues_get_str(0xC8);
  SGdiGetTextExtent(psz, static_cast<int>(strlen(psz)), &ext);
  SGdiExtTextOut(pSurface, 0x4F - ext.cx / 2, 0x46, &rc, 0x10000FF, 1, 0, psz,
                 -1);
  psz = glues_get_str(0xC9);
  SGdiGetTextExtent(psz, static_cast<int>(strlen(psz)), &ext);
  SGdiExtTextOut(pSurface, 0xBD - ext.cx / 2, 0x46, &rc, 0x10000FF, 1, 0, psz,
                 -1);
  psz = glues_get_str(0xA2);
  psnprintf(szLine, 0x800, psz, pszDisconnects);
  SGdiGetTextExtent(szLine, static_cast<int>(strlen(szLine)), &ext);
  SGdiExtTextOut(pSurface, 0x86 - ext.cx / 2, 0x78, &rc, 0x10000FF, 1, 0,
                 szLine, -1);
  SGdiSelectObject(hFontBig);
  SGdiGetTextExtent(pszWins, static_cast<int>(strlen(pszWins)), &ext);
  SGdiExtTextOut(pSurface, 0x4F - ext.cx / 2, 0x55, &rc, 0x10000FF, 1, 0,
                 pszWins, -1);
  SGdiGetTextExtent(pszLosses, static_cast<int>(strlen(pszLosses)), &ext);
  SGdiExtTextOut(pSurface, 0xBD - ext.cx / 2, 0x55, &rc, 0x10000FF, 1, 0,
                 pszLosses, -1);
  if (oc_d667B70 == 0) {
    SGdiSelectObject(hFontSmall);
    psz = glues_get_str(0x9C);
    SGdiExtTextOut(pSurface, 0x14, 0xAC, &rc, 0x10000FF, 1, 0, psz, -1);
    szLine[0] = '\0';
    if (_stricmp(sgProfileValues[7], szProfileZero) != 0) {
      char szDate[0x400];
      char szTime[0x400];
      SYSTEMTIME st;
      FILETIME ftLocal;
      FILETIME ft;
      const char *pszLastGame = sgProfileValues[7];

      ft.dwHighDateTime = strtoul(pszLastGame, 0, 10);
      psz = strchr(pszLastGame, ' ');
      if (psz != 0)
        ft.dwLowDateTime = strtoul(psz + 1, 0, 10);
      FileTimeToLocalFileTime(&ft, &ftLocal);
      FileTimeToSystemTime(&ftLocal, &st);
      GetDateFormatA(0x400, 1, &st, 0, szDate, 0x400);
      GetTimeFormatA(0x400, 0, &st, "HH':'mm", szTime, 0x400);
      SStrCopy(szLine, sgProfileValues[8], 5);
      SStrPack(szLine, " at ", 0x800);
      SStrPack(szLine, szDate, 0x800);
      SStrPack(szLine, " ", 0x800);
      SStrPack(szLine, szTime, 0x800);
    } else {
      psnprintf(szLine, 0x800, "%s", glues_get_str(0xA3));
    }
    SGdiExtTextOut(pSurface, 0x60, 0xAC, &rc, 0x10000FF, 1, 0, szLine, -1);
  } else {
    SGdiSelectObject(hFontSmall);
    cch = psnprintf(szLine, 0x800, glues_get_str(0x99));
    psnprintf(szLine + cch, 0x800 - cch, " %s", pszRating);
    SGdiGetTextExtent(szLine, static_cast<int>(strlen(szLine)), &ext);
    SGdiExtTextOut(pSurface, 0x86 - ext.cx / 2, 0xAC, &rc, 0x10000FF, 1, 0,
                   szLine, -1);
  }
  SGdiDeleteObject(hFontSmall);
  SGdiDeleteObject(hFontBig);
}


// Walks the paired key and value arrays SNet hands over and runs the callback
// on each pair. The list ends at the first empty key, not at a null pointer.
void sub_44EF20(char **ppKeys, char **ppValues, PFN_PROFILE_FIELD pfnField,
                void *pUnused)
{
  int i;

  if (ppKeys == 0 || ppValues == 0 || pfnField == 0)
    return;
  // The named key keeps 1.17's registers in sub_44F4C0's inlined copy.
  for (i = 0; *ppKeys[i] != '\0'; i++) {
    char *pszKey = ppKeys[i];
    pfnField(pszKey, ppValues[i], pUnused);
  }
}


// Builds one of the profile panel's fonts and hands it to Storm. The point size
// is scaled to 96 dpi and negated, which is Win32's "character height" sense.
void *UiGetProfileFont(int nIndex)
{
  void *pFont;
  void *hFont;

  const profile_font_t *pRow = &sgProfileFonts[nIndex];

  hFont = CreateFontA(-MulDiv(pRow->nPoints, 96, 72), 0, 0, 0, pRow->nWeight, 0,
                      0, 0, 0, 0, 0, 0, 0x20, pRow->pszFace);
  if (hFont == 0)
    return 0;
  SGdiImportFont(hFont, &pFont);
  DeleteObject(hFont);
  return pFont;
}


// Loads the profile background and the small button art, then hands the button
// art to Storm for the three control ids. Which background is used depends on
// whether the panel can be edited.
void sub_44EFF0(void *hDlgArg)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  blt_size_t size;
  int anCtrlIds[3];

  anCtrlIds[0] = 1;
  anCtrlIds[1] = 2;
  anCtrlIds[2] = 0;
  LoadArtFile(hDlg, GetParent(hDlg), "Popup", -1, 1,
              sgpfnProfileSave != 0
                  ? "glue\\battle.net\\backgrounds\\bnprofile.pcx"
                  : "glue\\battle.net\\backgrounds\\bnviewprofile.pcx",
              &sgpProfileArt, &size, 1);
  LoadArtFile(0, 0, "Button", -1, 1,
              "glue\\battle.net\\generic\\icons\\but_xsm.pcx",
              &sgpProfileCtrlArt, &size, 1);
  SDlgSetControlBitmaps(hDlg, anCtrlIds, 0, sgpProfileCtrlArt, &size, 1, -1);
}


// Opens a URL in the user's browser, with a message box when that fails. The
// second launch runs only when FindExecutableA fails: 1.17's test, kept as it
// is.
void sub_44F090(void *hDlgArg, char *pszUrl)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  char szCaption[64];
  char szExe[260];
  char szText[512];
  FILE *pf;

  if (reinterpret_cast<DWORD_PTR>(ShellExecuteA(
          reinterpret_cast<HWND>(SDrawGetFrameWindow(0)), "open", pszUrl, 0, 0,
          1)) > 32)
    return;
  pf = fopen("battle.htm", "wb");
  if (pf != 0)
    fclose(pf);
  if (reinterpret_cast<DWORD_PTR>(FindExecutableA("battle.htm", 0, szExe)) <= 32) {
    if (reinterpret_cast<DWORD_PTR>(ShellExecuteA(
            reinterpret_cast<HWND>(SDrawGetFrameWindow(0)), "open", szExe,
            pszUrl, 0, 1)) <= 32) {
      SetActiveWindow(reinterpret_cast<HWND>(SDrawGetFrameWindow(0)));
      ShowWindow(reinterpret_cast<HWND>(SDrawGetFrameWindow(0)), 9);
      sub_44A700(0x4B5, szText, 0x200);
      sub_44A700(0x4B6, szCaption, 0x40);
      UiMessageBoxCallback(hDlg, szText, szCaption, 0x30);
    }
  }
  DeleteFileA("battle.htm");
}


// Paints the profile panel. With no profile loaded only the blank plate is
// drawn; otherwise the field table is refilled from SNet's key/value pair and
// the bitmap and the text are laid over it.
BOOL __stdcall UiProfileBitmapCallback(int nArg0, int nArg1, int nArg2,
                                       int nArg3, void *pProfile, int nArg5,
                                       char **ppKeys, char **ppValues,
                                       void *pSurface, int nWidth, int nHeight)
{
  if (pProfile == 0) {
    UiProfileDrawBlank(pSurface, nWidth, nHeight);
    return TRUE;
  }
  memset(sgProfileValues, 0, 18 * sizeof(const char *));
  sub_44EF20(ppKeys, ppValues, sub_44FB40, 0);
  bn_blit_profile_img(pSurface, sgpProfileBmp, nWidth, nHeight);
  sub_44EAA0(pSurface, nWidth, nHeight);
  return TRUE;
}


// Enters the profile dialog. The six arguments the panel needs are filed in
// globals first; whether a save hook came with them picks the editable dialog
// over the read-only one.
BOOL __stdcall UiProfileCallback(int nArg0, int nArg1, void *pArg2, int nArg3,
                                 char *pszCaption, int nFieldCount, char **ppKeys,
                                 char **ppValues, PFN_PROFILE_SAVE pfnSave,
                                 int nArg9)
{
  const char *pszDlgName = pfnSave != 0 ? "DIALOG_PROFILE"
                                       : "DIALOG_STATIC_PROFILE";
  int nResult;

  oc_d667B7C = pszCaption;
  sgcProfileFields = nFieldCount;
  sgppProfileKeys = ppKeys;
  sgpfnProfileSave = pfnSave;
  sgppProfileValues = ppValues;
  oc_d667B70 = nArg9;
  nResult = gluBN_DoModalDlg(pszDlgName, reinterpret_cast<void **>(pArg2)[2],
                             glubn_profile_wndproc, 0);
  if (nResult == 0 || nResult == -1)
    return FALSE;
  return TRUE;
}


// Drops everything the profile dialog owns. The last slot is cleared whether or
// not it held anything; the two fonts are deleted and not cleared.
void sub_44F270()
{
  ClearBNText();
  if (sgpProfileArt != 0) {
    SMemFree(sgpProfileArt, "gluBNProfile.cpp", 0x28D, 0);
    sgpProfileArt = 0;
  }
  if (sgpProfileCtrlArt != 0) {
    SMemFree(sgpProfileCtrlArt, "gluBNProfile.cpp", 0x291, 0);
    sgpProfileCtrlArt = 0;
  }
  if (sghProfileFont1 != 0)
    DeleteObject(sghProfileFont1);
  if (sghProfileFont2 != 0)
    DeleteObject(sghProfileFont2);
  if (sgpProfileDescBuf != 0)
    SMemFree(sgpProfileDescBuf, "gluBNProfile.cpp", 0x430, 0);
  sgpProfileDescBuf = 0;
}


// The profile dialog's window procedure. Everything it does not handle goes to
// Storm's default proc, including the two messages it forwards to the frame
// window on the way past.
intptr_t __stdcall glubn_profile_wndproc(void *hDlgArg, unsigned int nMsg,
                                     uintptr_t wParam, LONG_PTR lParam)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  HWND hParent;

  switch (nMsg) {
    case WM_DESTROY:
      sub_44F270();
      Storm_228(hDlg);
      return FALSE;
    case WM_DRAWITEM:
      if (wParam == 0x412) {
        sub_44E930(hDlg, reinterpret_cast<DRAWITEMSTRUCT *>(lParam));
        return TRUE;
      }
      break;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      SendMessageA(reinterpret_cast<HWND>(SDrawGetFrameWindow(0)), nMsg, wParam,
                   lParam);
      break;
    case WM_INITDIALOG:
      sub_44F4C0(hDlg);
      return FALSE;
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case 1:
          EnableWindow(reinterpret_cast<HWND>(lParam), FALSE);
          sub_49E740(0xA7);
          if (sgpfnProfileSave != 0)
            sub_44F760(hDlg);
          SDlgEndDialog(hDlg, 1);
          return FALSE;
        case 2:
          EnableWindow(reinterpret_cast<HWND>(lParam), FALSE);
          sub_49E740(0xA7);
          SDlgEndDialog(hDlg, 0);
          return FALSE;
        case 0x412:
          if (HIWORD(wParam) == 0 && sgpfnProfileSave == 0) {
            sub_44E6E0(hDlg, reinterpret_cast<HWND>(lParam));
            return FALSE;
          }
          break;
      }
      break;
    case WM_SYSCOMMAND:
      if (wParam == SC_CLOSE) {
        hParent = GetParent(hDlg);
        SDlgEndDialog(hDlg, 0);
        PostMessageA(hParent, WM_SYSCOMMAND, SC_CLOSE, lParam);
        return FALSE;
      }
      break;
    case WM_CTLCOLORSTATIC:
      if (GetWindowLongA(reinterpret_cast<HWND>(lParam), GWL_ID) == 0x42D) {
        SetTextColor(reinterpret_cast<HDC>(wParam), 0xFFFF);
        return reinterpret_cast<intptr_t>(GetStockObject(5));
      }
      break;
  }
  return SDlgDefDialogProc(hDlg, nMsg, wParam, lParam);
}


// Builds the profile dialog: hides the four unused controls, sizes the fonts,
// fills the fields, and puts either the league image or the profile bitmap in
// the picture box. sub_44EAA0 runs after the join, on both arms.
void sub_44F4C0(void *hDlgArg)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  RECT rc;
  HWND hIcon;

  sub_44EFF0(hDlg);
  ShowWindow(GetDlgItem(hDlg, 0x40F), SW_HIDE);
  ShowWindow(GetDlgItem(hDlg, 0x42A), SW_HIDE);
  ShowWindow(GetDlgItem(hDlg, 0x40E), SW_HIDE);
  ShowWindow(GetDlgItem(hDlg, 0x429), SW_HIDE);
  sghProfileFont1 = sub_44FA60(hDlg, 2, sgProfileIconCtrlIds);
  sghProfileFont2 = sub_44FA60(hDlg, 1, sgProfileTextCtrlIds);
  SendMessageA(GetDlgItem(hDlg, 0x40D), WM_SETTEXT, 0,
               reinterpret_cast<LPARAM>(oc_d667B7C));
  sub_44EF20(sgppProfileKeys, sgppProfileValues, sub_44FB40, 0);
  hIcon = GetDlgItem(hDlg, 0x424);
  if (league_get_name() != 0 && oc_d667B70 != 0) {
    if (sgpProfileDescBuf != 0)
      SMemFree(sgpProfileDescBuf, "gluBNProfile.cpp", 0x40B, 0);
    sgpProfileDescBuf = league_load_profile_img();
    rc.left = 0;
    rc.top = 0;
    rc.right = 0x10D;
    rc.bottom = 0xC7;
  } else {
    GetClientRect(hIcon, &rc);
    sgpProfileDescBuf = SMemAlloc(sgProfileBmpWidth * sgProfileBmpHeight,
                                  "gluBNProfile.cpp", 0x407, 0);
    bn_blit_profile_img(sgpProfileDescBuf, sgpProfileBmp, rc.right + 1,
                        rc.bottom + 1);
  }
  sub_44EAA0(sgpProfileDescBuf, rc.right + 1, rc.bottom + 1);
  SDlgSetBitmapE(hIcon, hDlg, "", -1, 1, sgpProfileDescBuf,
                 reinterpret_cast<rect32_t *>(&rc), rc.right + 1, rc.bottom + 1,
                 -1);
  sub_44EF20(sgppProfileKeys, sgppProfileValues, sub_44F9C0, hDlg);
  if (sgpfnProfileSave != 0) {
    SetFocus(GetDlgItem(hDlg, 1));
  } else {
    sub_449BF0(GetDlgItem(hDlg, 0x412));
    EnableWindow(GetDlgItem(hDlg, 1), FALSE);
    SetFocus(GetDlgItem(hDlg, 2));
  }
}


// Collects every editable field whose control text has changed and hands the
// two parallel arrays to the save hook. Both arrays are always allocated at the
// full field count and both are released here.
void sub_44F760(void *hDlgArg)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  const profile_key_t *pTable;
  char **ppText;
  char **ppKey;
  const char *pszValue;
  const char *pszKey;
  int i;
  int j;
  int nSaved;
  int cchValue;
  int cchText;

  if (sgpfnProfileSave == 0)
    return;
  ppText = static_cast<char **>(SMemAlloc(sgcProfileFields * sizeof(char *),
                                          "gluBNProfile.cpp", 0x2CB, 0));
  ppKey = static_cast<char **>(SMemAlloc(sgcProfileFields * sizeof(char *),
                                         "gluBNProfile.cpp", 0x2CD, 0));
  pTable = gbIsExpansion ? sgSexpProfileKeyTable : sgStarProfileKeyTable;
  nSaved = 0;
  for (i = 0; i < sgcProfileFields; ++i) {
    pszValue = sgppProfileValues[i];
    pszKey = sgppProfileKeys[i];
    for (j = 0; j < 18; ++j) {
      if (_stricmp(pszKey, pTable[j].pszKey) != 0)
        continue;
      cchValue = static_cast<int>(strlen(pszValue));
      if ((pTable[j].dwFlags & 1) == 0)
        continue;
      cchText = (int)SendDlgItemMessageA(hDlg, pTable[j].nCtrlId, WM_GETTEXTLENGTH,
                                    0, 0);
      ppText[nSaved] = static_cast<char *>(
          SMemAlloc(cchText + 1, "gluBNProfile.cpp", 0x2E5, 0));
      SendDlgItemMessageA(hDlg, pTable[j].nCtrlId, WM_GETTEXT, cchText + 1,
                          reinterpret_cast<LPARAM>(ppText[nSaved]));
      ppText[nSaved][cchText] = '\0';
      if (cchText == cchValue
          && _strnicmp(pszValue, ppText[nSaved], cchText) == 0) {
        SMemFree(ppText[nSaved], "gluBNProfile.cpp", 0x2F7, 0);
        break;
      }
      ppKey[nSaved] = static_cast<char *>(
          SMemAlloc((DWORD)(strlen(pszKey) + 1), "gluBNProfile.cpp", 0x2F3, 0));
      strcpy(ppKey[nSaved], pszKey);
      ++nSaved;
      break;
    }
  }
  sgpfnProfileSave("", nSaved, ppKey, ppText);
  for (--nSaved; nSaved >= 0; --nSaved) {
    SMemFree(ppKey[nSaved], "gluBNProfile.cpp", 0x305, 0);
    SMemFree(ppText[nSaved], "gluBNProfile.cpp", 0x306, 0);
  }
  SMemFree(ppKey, "gluBNProfile.cpp", 0x308, 0);
  SMemFree(ppText, "gluBNProfile.cpp", 0x309, 0);
}


// Pushes one key/value pair into the profile dialog's controls. Rows flagged 4
// are the read-only record fields and are skipped; the length cap only goes out
// when the panel is editable.
void sub_44F9C0(const char *pszKey, const char *pszValue, void *hDlg)
{
  const profile_key_t *pTable = gbIsExpansion ? sgSexpProfileKeyTable
                                              : sgStarProfileKeyTable;
  const profile_key_t *pRow = pTable;
  HWND hCtrl;
  int i;

  for (i = 0; i < 18; ++i, ++pRow) {
    if (_stricmp(pszKey, pRow->pszKey) == 0)
      break;
  }
  if (i == 18)
    return;
  if ((pTable[i].dwFlags & 4) != 0)
    return;
  hCtrl = GetDlgItem(reinterpret_cast<HWND>(hDlg), pTable[i].nCtrlId);
  SendMessageA(hCtrl, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(pszValue));
  if (sgpfnProfileSave != 0)
    SendMessageA(hCtrl, EM_LIMITTEXT, pTable[i].cchMax, 0);
}


// Derives a font from the dialog's own and hands it to a list of controls. The
// row index is relative to the table's fifth entry, where the dialog sizes
// start.
void *sub_44FA60(void *hDlgArg, int nIndex, const int *pnCtrlIds)
{
  HWND hDlg = static_cast<HWND>(hDlgArg);
  profile_logfont_t lf;
  const profile_font_t *pRow;
  void *hFont;
  void *hOld;
  int i;

  hOld = reinterpret_cast<void *>(SendMessageA(hDlg, WM_GETFONT, 0, 0));
  if (hOld == 0)
    return 0;
  memset(&lf, 0, sizeof(lf));
  if (GetObjectA(hOld, sizeof(lf), &lf) == 0)
    return 0;
  pRow = &sgProfileFonts[nIndex + 4];
  lf.lfHeight = -MulDiv(pRow->nPoints, 96, 72);
  lf.lfWeight = pRow->nWeight;
  strcpy(lf.lfFaceName, pRow->pszFace);
  hFont = CreateFontIndirectA(&lf);
  if (hFont == 0)
    return 0;
  if (pnCtrlIds != 0) {
    for (i = 0; pnCtrlIds[i] != 0; ++i)
      SendDlgItemMessageA(hDlg, pnCtrlIds[i], WM_SETFONT,
                          reinterpret_cast<WPARAM>(hFont), 0);
  }
  return hFont;
}


// Records one key/value pair from SNet in the field-value table. An unknown key
// is dropped and an empty value is stored as "0".
void sub_44FB40(const char *pszKey, const char *pszValue, void *pUnused)
{
  const profile_key_t *pRow = gbIsExpansion ? sgSexpProfileKeyTable
                                            : sgStarProfileKeyTable;
  int i;

  for (i = 0; i < 18; ++i, ++pRow) {
    if (_stricmp(pszKey, pRow->pszKey) == 0)
      break;
  }
  if (i != 18)
    sgProfileValues[i] = *pszValue != '\0' ? pszValue : szProfileZero;
}


// Releases the two profile bitmaps. Each is freed only when non-null and the
// slot is cleared, so a second call is a no-op.
void bn_profile_free()
{
  if (sgpProfileBmp != 0) {
    SMemFree(sgpProfileBmp, "gluBNProfile.cpp", 0x4A4, 0);
    sgpProfileBmp = 0;
  }
  if (sgpBlankProfileBmp != 0) {
    SMemFree(sgpBlankProfileBmp, "gluBNProfile.cpp", 0x4A8, 0);
    sgpBlankProfileBmp = 0;
  }
}


// Picks the packed profile-key list for the running product. The expansion
// list is named first, 1.17's order.
char *UiGetProfileFields()
{
  if (gbIsExpansion)
    return sgSexpProfileKeys_g;
  return sgStarProfileKeys_g;
}


// Loads the profile background and the blank plate. Each is measured with a
// null-buffer call first, then allocated at width times height and reloaded.
void bn_profile_init()
{
  const char *pszFile = gbIsExpansion
                            ? "glue\\battle.net\\backgrounds\\profileexp.pcx"
                            : "glue\\battle.net\\backgrounds\\profilebkg.pcx";

  SBmpLoadImage(pszFile, 0, 0, 0, reinterpret_cast<DWORD *>(&sgProfileBmpWidth),
                reinterpret_cast<DWORD *>(&sgProfileBmpHeight), 0);
  sgpProfileBmp = SMemAlloc(sgProfileBmpWidth * sgProfileBmpHeight,
                            "gluBNProfile.cpp", 0x496, 0);
  SBmpLoadImage(pszFile, 0, sgpProfileBmp,
                sgProfileBmpWidth * sgProfileBmpHeight, 0, 0, 0);
  sgpBlankProfileBmp = SMemAlloc(sgProfileBmpWidth * sgProfileBmpHeight,
                                 "gluBNProfile.cpp", 0x499, 0);
  SBmpLoadImage("glue\\battle.net\\backgrounds\\blankprofile.pcx", 0,
                sgpBlankProfileBmp, sgProfileBmpWidth * sgProfileBmpHeight, 0, 0,
                0);
}
