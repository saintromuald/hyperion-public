// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBN.cpp -- the battle.net glue: the SNet UI callbacks, the on-screen text
// runs, and the resource image and its dialogs. Bodies are in oracle VA order.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBN.h"
#include "../h/League.h"
#include "../h/signature.h"   // sig_validate_certificate
#include "../h/rtti.h"    // type_info::raw_name -- OURS, not <typeinfo>
#include "../h/glues.h"   // sub_49E740, glues_reset_prev_pal
#include "../h/snd.h"     // load_game_data
#include "../h/netdesc.h"  // SNETPROGRAMDATA, SNETPLAYERDATA and their three initialisers
#include "../h/net_glue.h"  // IsGameCreator
#include "../h/save_dlg.h"  // RunEvtDispatcher
#include "../h/uiMulti.h"   // uiMulti_OnInitData
#include "../h/gluBNCreate.h"   // UiCreateGameCallback
#include "../h/gluBNChat.h"     // UiAuthCallback, UiDrawDescCallback, bn_chat_*
#include "../h/gluBNIcon.h"     // bn_icons_init, bn_icons_free
#include "../h/gluBNJoin.h"     // UiCategoryListCallback
#include "../h/gametype.h"      // UiLeagueCallback
#include "../h/Storm.h"   // SMemFree (pulls <windows.h> with NOGDI)
#include "../gen/globals117.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

// Callees in other translation units.

// Storm ordinal 208, through the import thunk at 0x4D00EE.
extern "C" int __stdcall SDlgDialogBoxIndirectParam(void *hInst, void *pTemplate,
                                                    void *hWndParent,
                                                    void *pfnDlgProc,
                                                    intptr_t nParam);

// The SNet UI callback table's other entries, reached only by address, so
// their arity does not change InitSNetUIData.
BOOL __stdcall UiProfileBitmapCallback(int nArg0, int nArg1, int nArg2,
                                       int nArg3, void *pProfile, int nArg5,
                                       char **ppKeys, char **ppValues,
                                       void *pSurface, int nWidth,
                                       int nHeight);
// Restated as gluBNProfile.h has it, the typedef included: a `void *` save hook
// mangles differently and the payload link cannot find the definition.
typedef void (__stdcall *PFN_PROFILE_SAVE)(const char *pszEmpty, int nCount,
                                           char **ppArg2, char **ppArg3);
BOOL __stdcall UiProfileCallback(int nArg0, int nArg1, void *pArg2, int nArg3,
                                 char *pszCaption, int nFieldCount, char **ppKeys,
                                 char **ppValues, PFN_PROFILE_SAVE pfnSave,
                                 int nArg9);
char *UiGetProfileFields();


// Storm ordinals 224 and 125, through the import thunks at 0x4D00FA and
// 0x4D0112. SNetGetPlayerName is spelled as net_player.cpp spells it.
extern "C" void __stdcall SDlgSetMessageLoopThrottle(DWORD dwEnabled);
extern "C" BOOL __stdcall SNetSelectGame(int nFlags, void *pProgramData,
                                         void *pPlayerData, void *pUIData,
                                         void *pArg4, DWORD *pdwPlayerId);
extern "C" BOOL __stdcall SNetGetPlayerName(DWORD dwPlayerId, char *pszBuffer,
                                            DWORD cchBuffer);

// 0x44FC10 / 0x44FBA0, gluBNProfile.cpp. Both nullary; every caller discards
// EAX.
void bn_profile_init();
void bn_profile_free();

// The Win32 imports, `__declspec(dllimport)` for 1.17's indirect calls. The
// GDI32 ones are restated because Storm.h defines NOGDI before <windows.h>.
extern "C" __declspec(dllimport) HWND __stdcall GetActiveWindow(void);
extern "C" __declspec(dllimport) HDC __stdcall CreateCompatibleDC(HDC hdc);
extern "C" __declspec(dllimport) HGDIOBJ __stdcall SelectObject(HDC hdc, HGDIOBJ h);
extern "C" __declspec(dllimport) BOOL __stdcall GetTextExtentPoint32A(
    HDC hdc, LPCSTR lpString, int c, LPSIZE psizl);
extern "C" __declspec(dllimport) BOOL __stdcall DeleteDC(HDC hdc);
extern "C" __declspec(dllimport) BOOL __stdcall GetTextMetricsA(HDC hdc, void *lptm);
extern "C" __declspec(dllimport) int __stdcall GetObjectA(HGDIOBJ h, int c, void *pv);
extern "C" __declspec(dllimport) HGDIOBJ __stdcall CreateFontIndirectA(const void *plf);
extern "C" __declspec(dllimport) LRESULT __stdcall SendMessageA(HWND hWnd, UINT uMsg,
                                                                 WPARAM wParam,
                                                                 LPARAM lParam);
extern "C" __declspec(dllimport) BOOL __stdcall DeleteObject(HGDIOBJ ho);
extern "C" __declspec(dllimport) HGDIOBJ __stdcall GetStockObject(int i);
extern "C" __declspec(dllimport) UINT __stdcall GetPaletteEntries(
    void *hPal, UINT iStart, UINT cEntries, void *pEntries);

// Storm ordinals 323, 328, 327, 222 and 357. SBmpLoadImage is spelled as
// gluBNProfile.h and gamedata.cpp spell it.
extern "C" BOOL __stdcall SBmpLoadImage(const char *pszFileName, void *pPalette,
                                        void *pBuffer, DWORD dwBufferSize,
                                        DWORD *pdwWidth, DWORD *pdwHeight,
                                        DWORD *pdwBpp);
extern "C" int __stdcall SBmpGetPitchForAlignment(int nWidth, int nAlign);
extern "C" BOOL __stdcall SBmpPadImage(void *pImage, DWORD cbImage,
                                       DWORD dwWidth, DWORD dwHeight,
                                       DWORD dwAlign, DWORD *pdwNewWidth);
extern "C" BOOL __stdcall SDlgSetBitmapE(void *hCtrl, void *hDlg, const char *pszName,
                              int nArg3, int nArg4, void *pBits, rect32_t *prc,
                              int cx, int cy, int nArg9);
extern "C" BOOL __stdcall SDrawUpdatePalette(DWORD dwFirst, DWORD dwCount,
                                             void *pPalette, DWORD dwUpdate);

// ComputeBaseUnits' body, forced inline: 1.17 inlines it at sub_449BF0's first
// call site and calls it at the second, a split MSVC does not make on its own.
static __forceinline int ComputeBaseUnitsInline(void *hFont);

// The globals this TU reaches, dual-spelled, one #ifdef per name.

// 0x66729C -- the loaded rez\gluBNRes.res image.
#ifndef OC_SGPRESOURCEFILE
#define OC_SGPRESOURCEFILE
#ifdef OC_DLL_MODE
#define sgpResourceFile (*reinterpret_cast<void **>(OC_VA(0x66729Cu)))
#else
extern void *sgpResourceFile;
#endif
#endif

// 0x6672A0 -- that image's byte count.
#ifndef OC_SGDWRESOURCEFILESIZE_G
#define OC_SGDWRESOURCEFILESIZE_G
#ifdef OC_DLL_MODE
#define sgdwResourceFileSize_g (*reinterpret_cast<DWORD *>(OC_VA(0x6672A0u)))
#else
extern DWORD sgdwResourceFileSize_g;
#endif
#endif

// 0x6672A4 -- the init guard, compared against 0 and stored 0 or 1.
#ifndef OC_SGBBNETINITIALIZED_G
#define OC_SGBBNETINITIALIZED_G
#ifdef OC_DLL_MODE
#define sgbBNETInitialized_g (*reinterpret_cast<int *>(OC_VA(0x6672A4u)))
#else
extern int sgbBNETInitialized_g;
#endif
#endif

// 0x66FD34 -- the process instance handle.
#ifndef OC_GHINST
#define OC_GHINST
#ifdef OC_DLL_MODE
#define ghInst (*reinterpret_cast<HINSTANCE *>(OC_VA(0x66FD34u)))
#else
extern HINSTANCE ghInst;
#endif
#endif

// 0x6AAA64 -- the CPU-throttle flag, handed straight to
// SDlgSetMessageLoopThrottle.
#ifndef OC_ISCPUTHROTTLEENABLED
#define OC_ISCPUTHROTTLEENABLED
#ifdef OC_DLL_MODE
#define isCPUThrottleEnabled (*reinterpret_cast<DWORD *>(OC_VA(0x6AAA64u)))
#else
extern DWORD isCPUThrottleEnabled;
#endif
#endif

// 0x6672A8 and 0x6672AC -- sub_449BF0's two font clones, plain and underlined;
// ClearBNText deletes them.
#ifndef OC_DWORD_6672A8
#define OC_DWORD_6672A8
#ifdef OC_DLL_MODE
#define dword_6672A8 (*reinterpret_cast<HGDIOBJ *>(OC_VA(0x6672A8u)))
#else
extern HGDIOBJ dword_6672A8;
#endif
#endif

#ifndef OC_DWORD_6672AC
#define OC_DWORD_6672AC
#ifdef OC_DLL_MODE
#define dword_6672AC (*reinterpret_cast<HGDIOBJ *>(OC_VA(0x6672ACu)))
#else
extern HGDIOBJ dword_6672AC;
#endif
#endif

// 0x6672B0 and 0x6672B4 -- the dialog base unit of each of the two fonts.
#ifndef OC_DWORD_6672B0
#define OC_DWORD_6672B0
#ifdef OC_DLL_MODE
#define dword_6672B0 (*reinterpret_cast<int *>(OC_VA(0x6672B0u)))
#else
extern int dword_6672B0;
#endif
#endif

#ifndef OC_DWORD_6672B4
#define OC_DWORD_6672B4
#ifdef OC_DLL_MODE
#define dword_6672B4 (*reinterpret_cast<int *>(OC_VA(0x6672B4u)))
#else
extern int dword_6672B4;
#endif
#endif

// 0x5107D4 -- 1.17's RTTI type descriptor for `struct _DRAWTEXT`, whose raw
// name tags the text runs' allocations. typeid() would emit a descriptor of its
// own.
#ifndef OC_GTYPEINFO_DRAWTEXT
#define OC_GTYPEINFO_DRAWTEXT
#ifdef OC_DLL_MODE
#define gTypeInfo_DRAWTEXT (*reinterpret_cast<type_info *>(OC_VA(0x5107D4u)))
#else
extern type_info gTypeInfo_DRAWTEXT;
#endif
#endif

// 0x5033E0 -- the text-run list head, twelve bytes, its sentinel link at +4.
typedef TSList<_DRAWTEXT, TSGetLink<_DRAWTEXT> > DRAWTEXTLIST;

#ifndef OC_SGDRAWTEXTLIST_G
#define OC_SGDRAWTEXTLIST_G
#ifdef OC_DLL_MODE
#define sgDrawTextList_g (*reinterpret_cast<DRAWTEXTLIST *>(OC_VA(0x5033E0u)))
#else
extern DRAWTEXTLIST sgDrawTextList_g;
#endif
#endif

// The bodies, in oracle VA order.

// Hyperion: PasswordInit (0x448F40) is in grid.cpp, its one caller left.

// Loads one Battle.net UI bitmap by art id into the caller's buffer. The path
// starts as the battle.net glue directory, and an unknown id loads that
// directory name unchanged, as 1.17 does.
BOOL __stdcall UiArtCallback(int nProvider, int nArtId, void *pPalette,
                             void *pBuffer, DWORD dwBufferSize, DWORD *pdwWidth,
                             DWORD *pdwHeight, DWORD *pdwBpp)
{
  char szPath[MAX_PATH] = "";

  strcpy(szPath, "glue\\battle.net\\");
  switch (static_cast<DWORD>(nArtId)) {
    case 0x0:
      strcat(szPath, "Backgrounds\\bn_bkg.pcx");
      break;
    case 0x2:
      strcat(szPath, "Backgrounds\\bnjoinbg.pcx");
      break;
    case 0x3:
      strcat(szPath, "generic\\popups\\hpopup.pcx");
      break;
    case 0x5:
      strcat(szPath, "generic\\icons\\but_xsm.pcx");
      break;
    case 0x6:
      strcat(szPath, "generic\\icons\\but_sml.pcx");
      break;
    case 0x7:
      strcat(szPath, "generic\\icons\\but_med.pcx");
      break;
    case 0x8:
      strcat(szPath, "generic\\icons\\but_lrg.pcx");
      break;
    case 0x1:
      strcat(szPath, "generic\\icons\\but_lrg.pcx");
      break;
    case 0x4:
      strcat(szPath, "generic\\popups\\lpopup.pcx");
      break;
    case 0xa:
      strcat(szPath, "generic\\icons\\prog_bg.pcx");
      break;
    case 0xb:
      strcat(szPath, "generic\\icons\\prog_fil.pcx");
      break;
    case 0xc:
      strcat(szPath, "generic\\popups\\spopup.pcx");
      break;
    case 0xd:
      strcat(szPath, "icons\\arrows.pcx");
      break;
    case 0xe:
      strcat(szPath, "icons\\thumb.pcx");
      break;
    case 0xf:
      strcat(szPath, "icons\\bar.pcx");
      break;
    case 0x10:
      strcat(szPath, "generic\\icons\\cmel.pcx");
      break;
    case 0x11:
      strcat(szPath, "generic\\icons\\cmml.pcx");
      break;
    case 0x12:
      strcat(szPath, "generic\\icons\\cmbl.pcx");
      break;
    case 0x13:
      strcat(szPath, "generic\\icons\\cmec.pcx");
      break;
    case 0x14:
      strcat(szPath, "generic\\icons\\cmmc.pcx");
      break;
    case 0x15:
      strcat(szPath, "generic\\icons\\cmbc.pcx");
      break;
    case 0x16:
      strcat(szPath, "generic\\icons\\cmer.pcx");
      break;
    case 0x17:
      strcat(szPath, "generic\\icons\\cmmr.pcx");
      break;
    case 0x18:
      strcat(szPath, "generic\\icons\\cmbr.pcx");
      break;
    case 0x19:
      strcat(szPath, "generic\\icons\\slgray.pcx");
      break;
    case 0x1f:
      strcat(szPath, "generic\\icons\\but_square.pcx");
      break;
    case 0x1a:
      strcat(szPath, "generic\\icons\\slthumb.pcx");
      break;
    case 0x1b:
      strcat(szPath, "generic\\icons\\slfocus.pcx");
      break;
    case 0x1c:
      strcat(szPath, "generic\\icons\\slleft.pcx");
      break;
    case 0x1d:
      strcat(szPath, "generic\\icons\\slmiddle.pcx");
      break;
    case 0x1e:
      strcat(szPath, "generic\\icons\\slright.pcx");
      break;
    case 0x20:
      strcat(szPath, "generic\\icons\\but_checkoff.pcx");
      break;
    case 0x80000000u:
      strcat(szPath, "icons\\bnbuttns.pcx");
      break;
    case 0x21:
      strcat(szPath, "generic\\icons\\but_checkon.pcx");
      break;
    case 0x80000001u:
      strcat(szPath, "Backgrounds\\chat_bkg.pcx");
      break;
    case 0x80000003u:
      strcat(szPath, "icons\\yellolag.pcx");
      break;
    case 0x80000002u:
      strcat(szPath, "icons\\greenlag.pcx");
      break;
    case 0x80000004u:
      strcat(szPath, "icons\\redlag.pcx");
      break;
    case 0x80000007u:
      strcat(szPath, "Backgrounds\\bnlogin.pcx");
      break;
    case 0x80000008u:
      strcat(szPath, "Backgrounds\\newaccount.pcx");
      break;
    case 0x80000009u:
      strcat(szPath, "Backgrounds\\changepassword.pcx");
      break;
    case 0x80000005u:
      strcat(szPath, "Backgrounds\\bnconnect.pcx");
      break;
    case 0x80000006u:
      strcat(szPath, "Backgrounds\\bnselchn.pcx");
      break;
    case 0x8000000au:
      strcat(szPath, "Backgrounds\\bnladder.pcx");
      break;
    case 0x8000000bu:
      strcat(szPath, "icons\\disconnected.pcx");
      break;
    case 0x8000000cu:
      strcat(szPath, "popups\\welcome.pcx");
      break;
    case 0x8000000du:
      strcat(szPath, "generic\\popups\\lepopup.pcx");
      break;
    case 0x8000000eu:
      strcat(szPath, "backgrounds\\tos.pcx");
      break;
    case 0x8000000fu:
      strcat(szPath, "Backgrounds\\registeremail.pcx");
      break;
  }
  if (szPath[0] != 0)
    return SBmpLoadImage(szPath, pPalette, pBuffer, dwBufferSize, pdwWidth,
                         pdwHeight, pdwBpp);
  return FALSE;
}

// Runs the Battle.net game-selection session. The program and player blocks
// are opaque here; only their sizes are known.
void sub_449990()
{
  SNETUIDATA ui;
  SNETPROGRAMDATA programData;
  SNETPLAYERDATA playerData;
  DWORD dwPlayerId;
  BOOL bSelected;

  InitSNetProgramData(&programData);
  InitSNetPlayerData(&playerData);
  InitSNetUIData(&ui);
  SDlgSetMessageLoopThrottle(isCPUThrottleEnabled);
  bSelected = SNetSelectGame(1, &programData, &playerData, &ui, NULL,
                             &dwPlayerId);
  DestroySNetProgramData(&programData);
  if (bSelected) {
    if (!SNetGetPlayerName(dwPlayerId, gLocalPlayerName, 0x19))
      gLocalPlayerName[0] = 0;
    glGluesMode = 3;
    g_LocalStormID = dwPlayerId;
    glue_fade_out();
    if (IsGameCreator("gluBN.cpp", 0x14C))
      return;
    if (uiMulti_OnInitData())
      return;
    glGluesMode = 4;
  } else {
    glue_fade_out();
    RunEvtDispatcher(0);
    glGluesMode = 2;
  }
}

// TSList<_DRAWTEXT>::UnlinkNode, which 1.17 inlines whole into DrawTextCreate.
// The link offset is computed in its own branch: written as one expression,
// MSVC reassociates the sum.
static void UnlinkDrawText(_DRAWTEXT *pNode)
{
  TSLink<_DRAWTEXT> *pl = &pNode->link;
  _DRAWTEXT *pPrev = pl->prev;

  if (pPrev != NULL) {
    _DRAWTEXT *pNext = pl->next;
    TSLink<_DRAWTEXT> *pnl;

    if (reinterpret_cast<intptr_t>(pNext) <= 0) {
      pnl = reinterpret_cast<TSLink<_DRAWTEXT> *>(~reinterpret_cast<intptr_t>(pNext));
    } else {
      intptr_t nOff = 0;

      if (reinterpret_cast<intptr_t>(pNext) > 0)
        nOff = reinterpret_cast<intptr_t>(pl) -
               reinterpret_cast<intptr_t>(pPrev->link.next);
      pnl = reinterpret_cast<TSLink<_DRAWTEXT> *>(reinterpret_cast<intptr_t>(pNext) +
                                                  nOff);
    }
    pnl->prev = pPrev;
    pl->prev->link.next = pl->next;
    pl->prev = NULL;
    pl->next = NULL;
  }
}

// 0x449930 (84 B). Unlinks and frees every text run. Normalising the head to
// null before the test keeps the loop unrotated, as in gametype's TTEMPLATE list.
void DRAWTEXTLIST::Clear(void)
{
  for (;;) {
    _DRAWTEXT *p = link.next;
    if ((intptr_t)p <= 0)
      p = 0;
    if (!p)
      break;
    UnlinkDrawText(p);
    SMemFree(p, gTypeInfo_DRAWTEXT.raw_name(), -2, 0);
  }
}

// 0x449A60 (103 B). Unlinks and frees pNode and returns its successor, or null
// at the terminator.
_DRAWTEXT *DRAWTEXTLIST::DeleteNode(_DRAWTEXT *pNode)
{
  TSLink<_DRAWTEXT> *pl = (pNode != NULL) ? &pNode->link : &link;
  _DRAWTEXT *pnext = pl->next;
  if ((intptr_t)pnext <= 0)
    pnext = 0;
  UnlinkDrawText(pNode);
  SMemFree(pNode, gTypeInfo_DRAWTEXT.raw_name(), -2, 0);
  return pnext;
}

// 0x449BD0 (24 B). Unlinks and frees one text run through the list's DeleteNode.
int RemoveBNText(_DRAWTEXT *pDraw)
{
  if (pDraw == NULL)
    return 0;
  sgDrawTextList_g.DeleteNode(pDraw);
  return 1;
}

// Allocates a text run, appends it to the list and measures the font. The node
// is filled in even when the allocation fails: 1.17 does not re-check it.
_DRAWTEXT *DrawTextCreate(void *hDC, const rect32_t *prc)
{
  void *pv = SMemAlloc(sizeof(_DRAWTEXT), gTypeInfo_DRAWTEXT.raw_name(), -2, 8);
  _DRAWTEXT *pNode;
  TSLink<_DRAWTEXT> *pnl;
  _DRAWTEXT *pTail;

  if (pv != NULL) {
    pNode = reinterpret_cast<_DRAWTEXT *>(pv);
    pNode->link.prev = NULL;
    pNode->link.next = NULL;
    pnl = &pNode->link;
  } else {
    pNode = NULL;
    pnl = &sgDrawTextList_g.link;
  }
  UnlinkDrawText(reinterpret_cast<_DRAWTEXT *>(pnl));
  pTail = sgDrawTextList_g.link.prev;
  pnl->prev = pTail;
  pnl->next = pTail->link.next;
  pTail->link.next = pNode;
  sgDrawTextList_g.link.prev = reinterpret_cast<_DRAWTEXT *>(pnl);
  pNode->hDC = hDC;
  pNode->rc = *prc;
  pNode->nField1C = 0;
  pNode->nField20 = 0;
  GetTextMetricsA(static_cast<HDC>(hDC), pNode->alTextMetric);
  pNode->nLineHeight = pNode->alTextMetric[4] + pNode->alTextMetric[0];
  return pNode;
}

// Drops the two cached fonts and every on-screen text run.
void ClearBNText()
{
  if (dword_6672A8 != NULL) {
    DeleteObject(dword_6672A8);
    dword_6672A8 = NULL;
  }
  if (dword_6672AC != NULL) {
    DeleteObject(dword_6672AC);
    dword_6672AC = NULL;
  }
  sgDrawTextList_g.Clear();
}

// Clones the window's font twice, plain and underlined, and caches each with
// its dialog base unit. The two clones are sequential, not nested: a failed
// first clone still tries the second.
int sub_449BF0(void *hWnd)
{
  oc_logfont_t lf;
  HGDIOBJ hFont;
  HGDIOBJ hClone;

  if (hWnd == NULL)
    return 0;
  hFont = reinterpret_cast<HGDIOBJ>(
      SendMessageA(static_cast<HWND>(hWnd), 0x31, 0, 0));
  if (hFont == NULL)
    return 0;
  memset(&lf, 0, sizeof(lf));
  if (GetObjectA(hFont, sizeof(lf), &lf)) {
    hClone = CreateFontIndirectA(&lf);
    if (hClone != NULL) {
      dword_6672A8 = hClone;
      dword_6672B0 = ComputeBaseUnitsInline(hClone);
    }
  }
  memset(&lf, 0, sizeof(lf));
  if (GetObjectA(hFont, sizeof(lf), &lf)) {
    lf.bUnderline = 1;
    hClone = CreateFontIndirectA(&lf);
    if (hClone != NULL) {
      dword_6672AC = hClone;
      dword_6672B4 = ComputeBaseUnits(hClone);
    }
  }
  return 1;
}

extern "C" __declspec(dllimport) COLORREF __stdcall SetTextColor(HDC hdc, COLORREF color);
extern "C" __declspec(dllimport) BOOL __stdcall GetTextExtentExPointA(
    HDC hdc, LPCSTR lpszString, int cchString, int nMaxExtent, LPINT lpnFit,
    LPINT lpnDx, LPSIZE lpSize);
extern "C" __declspec(dllimport) BOOL __stdcall ExtTextOutA(
    HDC hdc, int x, int y, UINT options, const void *lprect, LPCSTR lpString,
    UINT c, const INT *lpDx);
extern "C" int __cdecl _ismbcspace(unsigned int _Ch);

// Draws a string into a Battle.net text run with word wrap, "\r\n" line breaks
// and tab stops of eight base units. With prcHit the text is only measured and
// the rectangle it would cover is returned. nFit and size at function scope and
// the tab stop through a named int are 1.17's frame and arithmetic.
int sub_449D30(_DRAWTEXT *pDraw, char *pszText, int nKind, rect32_t *prcHit)
{
  if (pDraw == NULL)
    return 0;

  char *p = pszText;
  int nLen = (int)strlen(pszText);
  if (prcHit != NULL)
    memset(prcHit, 0, sizeof(rect32_t));

  HGDIOBJ hOldFont = NULL;
  HGDIOBJ hFont;
  int nTab;
  COLORREF clrOld;
  int nFit;
  SIZE size;
  if (nKind & 2) {
    hFont = dword_6672AC;
    nTab = dword_6672B4;
    // Four copies of one call, merged; the folded pDraw tests give the width's
    // subtractions below 1.17's order.
    if (pDraw) {
      if (pDraw)
        clrOld = SetTextColor(static_cast<HDC>(pDraw->hDC), 0xFF0000);
      else
        clrOld = SetTextColor(static_cast<HDC>(pDraw->hDC), 0xFF0000);
    } else {
      if (pDraw)
        clrOld = SetTextColor(static_cast<HDC>(pDraw->hDC), 0xFF0000);
      else
        clrOld = SetTextColor(static_cast<HDC>(pDraw->hDC), 0xFF0000);
    }
  } else {
    hFont = dword_6672A8;
    nTab = dword_6672B0;
    clrOld = SetTextColor(static_cast<HDC>(pDraw->hDC), 0xFFFFFF);
  }
  if (hFont != NULL)
    hOldFont = SelectObject(static_cast<HDC>(pDraw->hDC), hFont);
  if (prcHit != NULL) {
    prcHit->left = pDraw->nField1C;
    prcHit->top = pDraw->nField20;
  }

  do {
    while (nLen > 0 && *reinterpret_cast<WORD *>(p) == 0x0A0D) {
      pDraw->nField20 += pDraw->nLineHeight;
      pDraw->nField1C = 0;
      nLen -= 2;
      p += 2;
    }
    if (nLen == 0)
      break;

    nFit = 0;
    GetTextExtentExPointA(static_cast<HDC>(pDraw->hDC), p, nLen,
                          pDraw->rc.right - pDraw->nField1C - nTab - pDraw->rc.left + 1,
                          &nFit, NULL, &size);
    if (nFit == 0) {
      pDraw->nField1C = 0;
      pDraw->nField20 += pDraw->nLineHeight;
      continue;
    }

    if (nFit < nLen) {
      int i;
      for (i = nFit; i > 0; --i)
        if (_ismbcspace(p[i]))
          break;
      if (i > 0)
        nFit = i + 1;
      else if (pDraw->nField1C > 0)
        nFit = _ismbcspace(p[i]) ? i + 1 : i;
    }

    for (int j = 0; j < nFit; j++) {
      if (*reinterpret_cast<WORD *>(p + j) == 0x0A0D || p[j] == '\t') {
        nFit = j;
        break;
      }
    }

    if (prcHit != NULL) {
      if (pDraw->nField1C < prcHit->left)
        prcHit->left = pDraw->nField1C;
    } else {
      ExtTextOutA(static_cast<HDC>(pDraw->hDC), pDraw->nField1C, pDraw->nField20, 4,
                  &pDraw->rc, p, nFit, NULL);
    }
    GetTextExtentPoint32A(static_cast<HDC>(pDraw->hDC), p, nFit, &size);
    if (prcHit != NULL) {
      if (pDraw->nField1C + size.cx > prcHit->right)
        prcHit->right = pDraw->nField1C + size.cx;
      if (size.cy + pDraw->nField20 > prcHit->bottom)
        prcHit->bottom = size.cy + pDraw->nField20;
    }
    pDraw->nField1C += size.cx;

    if (nFit < nLen) {
      if (p[nFit] == '\t') {
        nFit++;
        int nStop = nTab << 3;
        pDraw->nField1C = pDraw->nField1C - pDraw->nField1C % nStop + nStop;
      } else {
        if (*reinterpret_cast<WORD *>(p + nFit) == 0x0A0D)
          nFit += 2;
        pDraw->nField20 += pDraw->nLineHeight;
        pDraw->nField1C = 0;
      }
    }
    nLen -= nFit;
    p += nFit;
  } while (nLen > 0);

  if (pDraw->nField1C > pDraw->rc.right - pDraw->rc.left - nTab + 1) {
    pDraw->nField20 += pDraw->nLineHeight;
    pDraw->nField1C = 0;
  }
  if (hOldFont != NULL)
    SelectObject(static_cast<HDC>(pDraw->hDC), hOldFont);
  SetTextColor(static_cast<HDC>(pDraw->hDC), clrOld);
  return 1;
}

// The horizontal dialog base unit for a font, measured the way the Win32 SDK
// documents it: the average width of the 52 letters, halved and rounded.
static __forceinline int ComputeBaseUnitsInline(void *hFont)
{
  HDC hdc = GetDC(GetDesktopWindow());
  HDC hdcMem = CreateCompatibleDC(hdc);
  HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);
  SIZE size;
  int nUnits = 0;

  if (GetTextExtentPoint32A(hdcMem,
                            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
                            52, &size))
    nUnits = (size.cx / 26 + 1) / 2;
  SelectObject(hdcMem, hOldFont);
  DeleteDC(hdcMem);
  ReleaseDC(GetDesktopWindow(), hdc);
  return nUnits;
}

int ComputeBaseUnits(void *hFont)
{
  return ComputeBaseUnitsInline(hFont);
}

// The SNet UI layer's data hook: two queries, each answering four bytes. The
// size check compares against the answer's own width rather than a literal.
BOOL __stdcall UiGetDataCallback(int nArg0, DWORD dwDataId, DWORD *pdwOut,
                                 DWORD dwSize, DWORD *pdwWritten)
{
  DWORD dwAnswer = 0;

  switch (dwDataId) {
    case 0x80000000:
      dwAnswer = 4;
      if (pdwOut != NULL) {
        if (dwSize >= dwAnswer)
          *pdwOut = 100;
        else
          return FALSE;
      }
      break;
    case 2:
      dwAnswer = 4;
      if (pdwOut != NULL) {
        HCURSOR hCursor;

        if (dwSize < dwAnswer)
          return FALSE;
        hCursor = LoadCursorA(ghInst, "CURSOR_LINK");
        *pdwOut = reinterpret_cast<DWORD_PTR>(hCursor);
        if (hCursor == NULL)
          return FALSE;
      }
      break;
  }
  if (pdwWritten != NULL)
    *pdwWritten = dwAnswer;
  return dwAnswer != 0;
}

// Find one entry in the loaded .res image: walk the header chain comparing the
// type ordinal at +0x0A, then either the wide name at +0x0C or the name
// ordinal at +0x0E depending on the header size. Returns the entry's data.
void *sub_44A0E0(const void *pRes, DWORD dwSize, int nType,
                 const wchar_t *pszName)
{
  const BYTE *p = static_cast<const BYTE *>(pRes);
  DWORD dwLeft = dwSize;

  while (dwLeft != 0) {
    const BYTE *pHdr = p;
    DWORD dwHdrSize = reinterpret_cast<const DWORD *>(p)[1];
    DWORD dwDataSize = reinterpret_cast<const DWORD *>(p)[0];

    p += dwHdrSize;
    if (dwDataSize != 0 && nType == *reinterpret_cast<const WORD *>(pHdr + 0x0A)) {
      if (dwHdrSize > 0x10) {
        DWORD dwNul = 0;

        if (*reinterpret_cast<const WORD *>(&dwNul) !=
                static_cast<WORD>(reinterpret_cast<DWORD_PTR>(pszName) >> 16) &&
            _wcsicmp(pszName, reinterpret_cast<const wchar_t *>(pHdr + 0x0C)) == 0)
          return const_cast<BYTE *>(p);
      } else if (*reinterpret_cast<const WORD *>(pHdr + 0x0E) ==
                 reinterpret_cast<DWORD_PTR>(pszName)) {
        return const_cast<BYTE *>(p);
      }
    }
    if ((dwDataSize & 3) != 0)
      dwDataSize = (dwDataSize >> 2) * 4 + 4;
    p += dwDataSize;
    dwLeft -= dwDataSize + dwHdrSize;
  }
  return NULL;
}

// The SNet UI layer's sound hook: two sounds.
BOOL __stdcall UiSoundCallback(int nArg0, int nSound, int nArg2)
{
  switch (nSound) {
    case 0:
      sub_49E740(0xA6);
      break;
    case 1:
      sub_49E740(0xA7);
      break;
  }
  return FALSE;
}

// Runs the Battle.net game-select screen: brings the resources up, hands
// control to sub_449990, then tears them down. 1.17 inlines glubn_init and
// glubn_free here.
void do_bnet_dlg()
{
  glubn_init();
  sub_449990();
  glubn_free();
  glues_reset_prev_pal();
}

// Loads a .pcx into a freshly allocated 4-byte-aligned bitmap, optionally binds
// it to a dialog control, and publishes the Windows static palette colours when
// no control name was given.
BOOL LoadArtFile(void *pCtrl, void *pDlg, const char *pszName, int nArg3,
                 int nArg4, const char *pszFile, void **ppBits,
                 blt_size_t *pSize, int nFlag)
{
  DWORD adwPalette[256];
  DWORD dwWidth;
  DWORD dwHeight;
  DWORD cbBits;

  if (!SBmpLoadImage(pszFile, NULL, NULL, 0, &dwWidth, &dwHeight, NULL))
    return FALSE;
  cbBits = SBmpGetPitchForAlignment(dwWidth, 4) * dwHeight;
  *ppBits = SMemAlloc(cbBits, "gluBN.cpp", 0x234, 0);
  if (!SBmpLoadImage(pszFile, adwPalette, *ppBits, dwWidth * dwHeight, NULL,
                     NULL, NULL))
    return FALSE;
  if (pCtrl != NULL || pDlg != NULL) {
    if (!SDlgSetBitmapE(pCtrl, pDlg, pszName, nArg3, nArg4, *ppBits, NULL,
                        dwWidth, dwHeight, -1))
      return FALSE;
  }
  SBmpPadImage(*ppBits, cbBits, dwWidth, dwHeight, 4, &dwWidth);
  if (pszName == NULL || *pszName == 0) {
    HGDIOBJ hPalette = GetStockObject(15);

    GetPaletteEntries(hPalette, 0, 10, &adwPalette[0]);
    GetPaletteEntries(hPalette, 10, 10, &adwPalette[246]);
    if (nFlag != 0) {
      SDrawUpdatePalette(0, 0xFF, adwPalette, 1);
    } else {
      memset(adwPalette, 0, sizeof(adwPalette));
      SDrawUpdatePalette(0, 0x100, adwPalette, 1);
    }
  }
  if (pSize != NULL) {
    pSize->cx = dwWidth;
    pSize->cy = dwHeight;
  }
  return TRUE;
}

// Runs a dialog out of the Battle.net resource image. An ordinal name goes to
// the lookup as is; a string name is widened into a scratch buffer first,
// through a short, as 1.17 copies it.
int gluBN_DoModalDlg(const char *pszTemplate, void *hWndParent,
                     void *pfnDlgProc, intptr_t nParam)
{
  void *pTemplate;

  if (HIWORD(pszTemplate) == 0) {
    pTemplate = sub_44A0E0(
        sgpResourceFile, sgdwResourceFileSize_g, 5,
        reinterpret_cast<const wchar_t *>(
            static_cast<WORD>(reinterpret_cast<DWORD_PTR>(pszTemplate))));
    if (pTemplate == NULL)
      return 0;
  } else {
    DWORD cchName = SStrLen(pszTemplate) + 1;
    wchar_t *pwszName = static_cast<wchar_t *>(
        SMemAlloc(cchName * 2, "gluBN.cpp", 0x1C0, 0));
    wchar_t *pw = pwszName;
    const char *ps = pszTemplate;

    while (*ps != 0) {
      short s = *ps++;
      *pw++ = s;
    }
    *pw = 0;
    pTemplate = sub_44A0E0(sgpResourceFile, sgdwResourceFileSize_g, 5, pwszName);
    SMemFree(pwszName, "gluBN.cpp", 0x1CD, 0);
    if (pTemplate == NULL)
      return 0;
  }
  return SDlgDialogBoxIndirectParam(ghInst, pTemplate, hWndParent, pfnDlgProc,
                                    nParam);
}

// Puts up a Battle.net message box on the active window, falling back to the
// Storm frame window.
void BNMessageBox(const char *pszCaption, const char *pszText)
{
  HWND hWnd = GetActiveWindow();

  if (hWnd == NULL)
    hWnd = static_cast<HWND>(SDrawGetFrameWindow(0));
  UiMessageBoxCallback(hWnd, pszCaption, pszText, 0);
}

// Releases the Battle.net resource image and the three subsystems that share
// its lifetime.
void glubn_free()
{
  if (sgbBNETInitialized_g == 0)
    return;
  bn_chat_free();
  bn_profile_free();
  bn_icons_free();
  SMemFree(sgpResourceFile, "gluBN.cpp", 0x166, 0);
  sgbBNETInitialized_g = 0;
}

// Fills in the Battle.net UI's callback table for Storm's SNet layer.
void InitSNetUIData(SNETUIDATA *pUIData)
{
  memset(pUIData, 0, sizeof(SNETUIDATA));
  pUIData->dwSize = sizeof(SNETUIDATA);
  pUIData->dwVersion = 1;
  pUIData->hParentWnd = SDrawGetFrameWindow(0);
  pUIData->pfnArt = UiArtCallback;
  pUIData->pfnCreateGame = UiCreateGameCallback;
  pUIData->pfnDrawDesc = UiDrawDescCallback;
  pUIData->pfnMessageBox = UiMessageBoxCallback;
  pUIData->pfnSound = UiSoundCallback;
  pUIData->pfnAuth = UiAuthCallback;
  pUIData->pfnGetData = UiGetDataCallback;
  pUIData->pfnProfile = UiProfileCallback;
  pUIData->pfnCategoryList = UiCategoryListCallback;
  pUIData->pfnProfileBitmap = UiProfileBitmapCallback;
  pUIData->pProfileFields = UiGetProfileFields();
  pUIData->pfnLeague = UiLeagueCallback;
  pUIData->pfnValidateCertificate = sig_validate_certificate;
  pUIData->pfnSetLeague = UiSetLeagueCallback;
}


// Parses one space-separated Battle.net record into up to six out-parameters,
// then skips four more fields and copies the four-byte tag that follows.
int glubn_get_stats(void *pArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4,
               int *pnOut, intptr_t nArg6)
{
  const char *p = strchr(static_cast<const char *>(pArg0), ' ');

  if (p == NULL)
    return 0;
  p++;
  if (nArg1 != 0)
    *reinterpret_cast<DWORD *>(nArg1) = strtoul(p, NULL, 10);
  p = strchr(p, ' ');
  if (p == NULL)
    return 0;
  p++;
  if (nArg2 != 0)
    *reinterpret_cast<DWORD *>(nArg2) = strtoul(p, NULL, 10);
  p = strchr(p, ' ');
  if (p == NULL)
    return 0;
  p++;
  if (nArg3 != 0)
    *reinterpret_cast<DWORD *>(nArg3) = strtoul(p, NULL, 10);
  p = strchr(p, ' ');
  if (p == NULL)
    return 0;
  p++;
  if (nArg4 != 0)
    *reinterpret_cast<DWORD *>(nArg4) = (strtoul(p, NULL, 10) != 0);
  p = strchr(p, ' ');
  if (p == NULL)
    return 0;
  p++;
  if (pnOut != NULL)
    *pnOut = strtoul(p, NULL, 10);
  p = strchr(p, ' ');
  if (p == NULL)
    return 0;
  p = strchr(p + 1, ' ');
  if (p == NULL)
    return 0;
  p = strchr(p + 1, ' ');
  if (p == NULL)
    return 0;
  p = strchr(p + 1, ' ');
  if (p == NULL)
    return 0;
  if (nArg6 != 0)
    *reinterpret_cast<DWORD *>(nArg6) = *reinterpret_cast<const DWORD *>(p + 1);
  return 1;
}

// Loads rez\gluBNRes.res and brings up the three Battle.net subsystems, once.
void glubn_init()
{
  if (sgbBNETInitialized_g != 0)
    return;
  bn_chat_init();
  bn_profile_init();
  bn_icons_init();
  sgpResourceFile = load_game_data("rez\\gluBNRes.res", NULL,
                                   &sgdwResourceFileSize_g, "gluBN.cpp", 0x160);
  sgbBNETInitialized_g = 1;
}

// Copies one string out of the resource image's RT_STRING block into an ANSI
// buffer. The block holds sixteen counted wide strings; the id's low four bits
// pick one. The copy loop pre-increments both cursors, as 1.17 does.
int sub_44A700(int nId, char *pszBuf, DWORD cchBuf)
{
  const WORD *p = static_cast<const WORD *>(
      sub_44A0E0(sgpResourceFile, sgdwResourceFileSize_g, 6,
                 reinterpret_cast<const wchar_t *>(static_cast<WORD>(nId))));
  if (p == NULL)
    return 0;
  int nCount;
  int nLen;
  char *pDst;

  nCount = nId & 0x0F;
  while (nCount > 0) {
    nCount--;
    p += *p + 1;
  }
  nLen = *p;
  if (static_cast<DWORD>(nLen) >= cchBuf - 1)
    nLen = cchBuf - 1;
  pDst = pszBuf - 1;
  nCount = nLen;
  while (nCount > 0) {
    *++pDst = static_cast<char>(*++p);
    nCount--;
  }
  pDst[1] = 0;
  return nLen;
}
