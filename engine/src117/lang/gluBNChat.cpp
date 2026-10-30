// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNChat.cpp -- the Battle.net chat channel list. Bodies are in oracle VA order.
//
// TU membership cuts both ways and needs re-checking after a change here. A callee
// defined in this file can be spliced into a caller in this file by /Ob2; the oracle is
// WPO-off, so a cross-TU call cannot be, and the opposite case is a body whose size is
// exactly caller plus callee, which needs its callee here. The lever when a callee must
// live here anyway is __declspec(noinline) on the declaration.

#include "oc_data.h"  // Hyperion: OC_VA
#include "../h/gluBNChat.h"
#include "../h/gluBN.h"       // LoadArtFile, blt_size_t
#include "../h/gluBNIcon.h"   // bn_icons_get_size
#include "../h/glues.h"       // glues_get_str, IsSpawnedCopy
#include "../h/netdesc.h"     // gamedesc_t, Txt2GameData
#include "../h/net_glue.h"    // SetCreator
#include "../h/gametype.h"    // TGAMEPARAMS, IsCompetitiveGame
#include "../h/gluBNCreate.h" // gametype_find_template
#include "../h/gds_local.h"   // psnprintf
#include "../h/rect32.h"
#include <string.h>
#include <time.h>          // _localtime64 -- sub_44A980
#include "../h/Storm.h"       // SMemAlloc / SMemFree (pulls <windows.h> with NOGDI)
#include "../gen/globals117.h"

// Storm ordinals 445 and 434 (thunks 0x4D012A -> [0x4EB560] and 0x4D0130 ->
// [0x4EB4E8]). Neither is in Storm.h; STransDelete is spelled as
// gds_vidblit.cpp spells it. STransCreateE's fifth word is a source rectangle
// here, where gds_vidblit.cpp declares it `int` and passes 0.
extern "C" {
BOOL __stdcall STransCreateE(const void *pBits, int nWidth, int nHeight,
                             int nBpp, const rect32_t *prc, DWORD dwFlags,
                             HSTRANS *phTrans);
BOOL __stdcall STransDelete(HSTRANS hTrans);
BOOL __stdcall STransBlt(void *pDest, int x, int y, int nPitch, HSTRANS hTrans);
}

// GDI32 SetTextColor [0x4EB050], SetBkColor [0x4EB078], ExtTextOutA
// [0x4EB07C] and GetTextMetricsA [0x4EB084], behind NOGDI.
extern "C" {
__declspec(dllimport) DWORD __stdcall SetTextColor(void *hdc, DWORD crColor);
__declspec(dllimport) DWORD __stdcall SetBkColor(void *hdc, DWORD crColor);
__declspec(dllimport) BOOL __stdcall ExtTextOutA(void *hdc, int x, int y,
                                                 UINT uOptions, const RECT *prc,
                                                 const char *psz, UINT cch,
                                                 const int *pDx);
__declspec(dllimport) BOOL __stdcall GetTextMetricsA(void *hdc, void *ptm);
__declspec(dllimport) BOOL __stdcall TextOutA(void *hdc, int x, int y,
                                              const char *psz, int cch);
__declspec(dllimport) BOOL __stdcall MoveToEx(void *hdc, int x, int y,
                                              POINT *ppt);
__declspec(dllimport) UINT __stdcall SetTextAlign(void *hdc, UINT uAlign);
__declspec(dllimport) BOOL __stdcall GetTextExtentPoint32A(void *hdc,
                                                           const char *psz,
                                                           int cch, SIZE *psz2);
}

// GetTextMetricsA's out-parameter, 0x38 bytes on the frame; only the height and
// the external leading are read.
struct bn_textmetric_t {
  /*0x00*/ LONG tmHeight;
  /*0x04*/ BYTE _unread04[0x0C];
  /*0x10*/ LONG tmExternalLeading;
  /*0x14*/ BYTE _unread14[0x24];
};
static_assert(offsetof(bn_textmetric_t, tmHeight) == 0x00, "[ebp-4Ch]");
static_assert(offsetof(bn_textmetric_t, tmExternalLeading) == 0x10, "sub_44A980: mov eax, dword ptr [ebp-13Ch]");
static_assert(sizeof(bn_textmetric_t) == 0x38, "TEXTMETRICA");

// ---------------------------------------------------------------------------
// Globals this TU reaches, dual-spelled. One #ifdef per name, never one over
// the block: gen emits only marked rows' globals, so the set grows mid-wave and
// a single guard would undeclare every name under it.
//
// Before coining a name, look up re/globals117.json by VA key and check
// sc-refs/sheets for an existing one. Minting a name is the master's call.

// 0x6672B8 -- the twenty digit masks, two rows of ten cut from number.pcx.
// Name from sc-refs/sheets/data_naming_1.17.csv (gluBNChat.cpp).
#ifndef OC_SGTRANSNUM
#define OC_SGTRANSNUM
#ifdef OC_DLL_MODE
#define sgTransNum (reinterpret_cast<HSTRANS *>(OC_VA(0x6672B8u)))
#else
extern HSTRANS sgTransNum[20];
#endif
#endif

// 0x667308 -- a scratch buffer sized by icon 8's width times height.
#ifndef OC_DWORD_667308
#define OC_DWORD_667308
#ifdef OC_DLL_MODE
#define dword_667308 (*reinterpret_cast<BYTE **>(OC_VA(0x667308u)))
#else
extern BYTE *dword_667308;
#endif
#endif

// 0x66730C / 0x667310 -- icon 8's width and height.
#ifndef OC_DWORD_66730C
#define OC_DWORD_66730C
#ifdef OC_DLL_MODE
#define dword_66730C (*reinterpret_cast<int *>(OC_VA(0x66730Cu)))
#else
extern int dword_66730C;
#endif
#endif

#ifndef OC_DWORD_667310
#define OC_DWORD_667310
#ifdef OC_DLL_MODE
#define dword_667310 (*reinterpret_cast<int *>(OC_VA(0x667310u)))
#else
extern int dword_667310;
#endif
#endif

// 0x667314 -- number.pcx's width.
#ifndef OC_DWORD_667314
#define OC_DWORD_667314
#ifdef OC_DLL_MODE
#define dword_667314 (*reinterpret_cast<int *>(OC_VA(0x667314u)))
#else
extern int dword_667314;
#endif
#endif

// 0x6728F8 / 0x6728F9 / 0x6728FA -- the joined game's type, subtype and
// template word, the +0Ch..+0Eh fields of the game data record at 0x6728EC.
#ifndef OC_BYTE_6728F8
#define OC_BYTE_6728F8
#ifdef OC_DLL_MODE
#define byte_6728F8 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F8u)))
#else
extern BYTE byte_6728F8;
#endif
#endif

#ifndef OC_BYTE_6728F9
#define OC_BYTE_6728F9
#ifdef OC_DLL_MODE
#define byte_6728F9 (*reinterpret_cast<BYTE *>(OC_VA(0x6728F9u)))
#else
extern BYTE byte_6728F9;
#endif
#endif

#ifndef OC_WORD_6728FA
#define OC_WORD_6728FA
#ifdef OC_DLL_MODE
#define word_6728FA (*reinterpret_cast<WORD *>(OC_VA(0x6728FAu)))
#else
extern WORD word_6728FA;
#endif
#endif

// 0x4ECB98 -- the game-speed names: seven {string id, speed} pairs, indexed by
// the record's speed (`movsx ecx, word ptr [eax*4+4ECB98h]`). Name from
// sc-refs/sheets/data_naming_1.17.csv (gluBNChat.cpp .rdata).
struct bn_speed_name_t {
  /*0x00*/ short nStrId;
  /*0x02*/ short nSpeed;
};
static_assert(offsetof(bn_speed_name_t, nStrId) == 0x00, "sub_44A980: movsx ecx, word ptr [eax*4+4ECB98h]");
static_assert(offsetof(bn_speed_name_t, nSpeed) == 0x02, ".rdata 0x4ECB9A: 0, 1, 2 .. 6");
static_assert(sizeof(bn_speed_name_t) == 0x04, "the eax*4 stride");
#ifndef OC_STRU_4ECB98
#define OC_STRU_4ECB98
#ifdef OC_DLL_MODE
#define stru_4ECB98 (reinterpret_cast<const bn_speed_name_t *>(OC_VA(0x4ECB98u)))
#else
extern const bn_speed_name_t stru_4ECB98[7];
#endif
#endif

// ---------------------------------------------------------------------------
// The bodies, in oracle VA order.

// 0x44A780 (346 B). Vets the game a join is aimed at: records its type, then
// refuses a spawned copy of a ladder template or a player whose record does not
// qualify, writing the reason into pszError.
BOOL __stdcall UiAuthCallback(int nType, int nArg1, const char *pszInfo,
                              int nArg3, const char *pszGameData,
                              char *pszError, DWORD cchError)
{
  gamedesc_t desc;
  const TGAMEPARAMS *pParams;

  if (nType == 1) {
    SStrCopy(pszError, glues_get_str(0x96), cchError);
    return FALSE;
  }
  if (nType == 2) {
    if (!Txt2GameData(pszGameData, &desc)) {
      SStrCopy(pszError, glues_get_str(0x95), cchError);
      return FALSE;
    }
    byte_6728F8 = desc.bGameType;
    byte_6728F9 = desc.bGameSubType;
    word_6728FA = desc.wField0E;
    SetCreator(0);
    pParams = gametype_find_template(desc.bGameType, desc.bGameSubType, desc.wField0E);
    if (pParams) {
      if (pParams->bFlag12 == 1) {
        if (IsSpawnedCopy()) {
          SStrCopy(pszError, glues_get_str(0x94), cchError);
          return FALSE;
        }
        nType = 0;
        if (glubn_get_stats(const_cast<char *>(pszInfo), 0, 0, reinterpret_cast<intptr_t>(&nType), 0, 0, 0)) {
          if (nType < 10) {
            SStrCopy(pszError, glues_get_str(0x93), cchError);
            return FALSE;
          }
        } else {
          SStrCopy(pszError, glues_get_str(0x95), cchError);
          return FALSE;
        }
      }
      if (IsCompetitiveGame(pParams->bCategory, pParams->bSub)) {
        BOOL bQualified = FALSE;
        nType = 0;
        if (glubn_get_stats(const_cast<char *>(pszInfo), 0, 0, 0, 0, &nType, 0))
          bQualified = nType == pParams->bSub;
        if (bQualified) {
          if (IsSpawnedCopy()) {
            SStrCopy(pszError, glues_get_str(0xC2), cchError);
            return FALSE;
          }
        } else {
          SStrCopy(pszError, glues_get_str(0xC0), cchError);
          return FALSE;
        }
      }
    }
    return TRUE;
  }
  return TRUE;
}

// 0x44A8E0 (153 B). Paints one row: white text on a grey or black ground by
// the selected bit, one of two painters by nType, then the colours restored.
BOOL __stdcall UiDrawDescCallback(int nArg0, int nType, const char *pszName,
                                  const char *pszRecord, int nFlags, int nStyle,
                                  int nArg6, BYTE *pBits, int nNumber,
                                  tagDRAWITEMSTRUCT *pDraw)
{
  UINT bSelected = pDraw->itemState & 1;
  DWORD crText = SetTextColor(pDraw->hDC, 0xFFFFFF);
  DWORD crBk = SetBkColor(pDraw->hDC, bSelected ? 0xC8C8C8 : 0);

  if (nType == 1)
    sub_44A980(pszName, pszRecord, nFlags, nStyle, nArg6, pDraw);
  else if (nType == 2)
    sub_44AEC0(pszName, pszRecord, nFlags, nStyle, nArg6, pBits, nNumber, pDraw);
  SetTextColor(pDraw->hDC, crText);
  SetBkColor(pDraw->hDC, crBk);
  return TRUE;
}


// 0x44A980 (1310 B). Paints a game row, one line each: the game name, the
// status with its approval icon and the map, the template, the template's
// parameter, the speed, and the creation time when nArg6 is set. nArg6 is a
// 32-bit time handed to _localtime64 by address, so the high half it reads is
// the next stack slot.
void sub_44A980(const char *pszName, const char *pszRecord, int nFlags,
                int nStyle, int nArg6, tagDRAWITEMSTRUCT *pDraw)
{
  char szLine[0x80] = {0};

  if (*pszRecord == 0 && *pszName == 0)
    return;
  if (nStyle & 1) {
    ExtTextOutA(pDraw->hDC, pDraw->rcItem.left, pDraw->rcItem.top, 6,
                &pDraw->rcItem, pszName, (UINT)strlen(pszName), NULL);
    return;
  }
  if ((nStyle & 2) == 0)
    return;

  gamedesc_t gd;
  if (!Txt2GameData(pszRecord, &gd))
    return;

  bn_textmetric_t tm;
  POINT pt;
  SIZE size;
  int nY = pDraw->rcItem.top;
  GetTextMetricsA(pDraw->hDC, &tm);
  int nLineHeight = tm.tmHeight + tm.tmExternalLeading;

  SStrCopy(szLine, glues_get_str(0x89), 0x80);
  SStrPack(szLine, gd.szGameName, 0x80);
  MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, &pt);
  TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
  SetTextAlign(pDraw->hDC, 1);
  nY += nLineHeight;

  SStrCopy(szLine, glues_get_str(0x88), 0x80);
  MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, &pt);
  TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
  GetTextExtentPoint32A(pDraw->hDC, szLine, SStrLen(szLine), &size);
  int nX = size.cx + 1;
  int nIcon;
  switch (gd.bApproval) {
  case 1: nIcon = 0x19; break;
  case 2: nIcon = 0x1A; break;
  case 3: nIcon = 0x1B; break;
  case 4: nIcon = 0x1E; break;
  case 5: nIcon = 0x45; break;
  case 6: nIcon = 0x47; break;
  default: goto map;
  }
  sub_44DFB0(pDraw->hwndItem, pDraw->rcItem.left + nX, nY, nIcon);
  nX += 3 + bn_icons_get_size(nIcon)->cx;
map:
  SStrCopy(szLine, gd.szMapName, 0x80);
  MoveToEx(pDraw->hDC, pDraw->rcItem.left + nX, nY, &pt);
  TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
  UINT uAlign = SetTextAlign(pDraw->hDC, 1);
  nY += nLineHeight;

  const char *pszTemplate = gametype_find_template_name(gd.bGameType, gd.bGameSubType);
  SStrCopy(szLine, glues_get_str(0x8A), 0x80);
  SStrPack(szLine, pszTemplate, 0x80);
  MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, NULL);
  TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
  nY += nLineHeight;

  const char *pszParam = gametype_find_param_name(gd.bGameType, gd.bGameSubType);
  if (pszParam != NULL && *pszParam != 0) {
    char szSub[0x400];
    const TGAMEPARAMS *pParams =
        gametype_find_template(gd.bGameType, gd.bGameSubType, gd.wField0E);
    if (pParams != NULL)
      gametype_get_subtype_name(szSub, 0x400, pParams, gd.bMaxPlayers);
    else
      szSub[0] = 0;
    SStrCopy(szLine, pszParam, 0x80);
    SStrPack(szLine, ": ", 0x80);
    SStrPack(szLine, szSub, 0x80);
    MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, NULL);
    TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
    nY += nLineHeight;
  }

  SStrCopy(szLine, glues_get_str(0x8B), 0x80);
  SStrPack(szLine, glues_get_str(stru_4ECB98[gd.bGameSpeed].nStrId), 0x80);
  MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, NULL);
  TextOutA(pDraw->hDC, 0, 0, szLine, SStrLen(szLine));
  nY += nLineHeight;

  if (nArg6 != 0) {
    char szFormat[0x20];
    char szAmPm[10];
    SetTextAlign(pDraw->hDC, 1);
    SStrCopy(szFormat, glues_get_str(0x8C), 0x20);
    struct tm *pTime = _localtime64(reinterpret_cast<const __time64_t *>(&nArg6));
    if (pTime != NULL) {
      int bPM;
      // bPM before the subtraction, or the new hour is built in ECX.
      if (pTime->tm_hour > 12) {
        bPM = 1;
        pTime->tm_hour -= 12;
      } else {
        bPM = 0;
        if (pTime->tm_hour == 0)
          pTime->tm_hour = 12;
      }
      SStrCopy(szAmPm, glues_get_str(0x8D + bPM), 10);
      psnprintf(szLine, 0x80, szFormat, pTime->tm_hour, pTime->tm_min, szAmPm);
      MoveToEx(pDraw->hDC, pDraw->rcItem.left, nY, NULL);
      TextOutA(pDraw->hDC, 0, 0, szLine, (int)strlen(szLine));
    }
  }
  MoveToEx(pDraw->hDC, pt.x, pt.y, NULL);
  SetTextAlign(pDraw->hDC, uAlign);
}

// 0x44AEC0 (433 B). Paints a user row: an icon picked by the flags, the
// product tag or the ping record, then the name beside it. The switch repeats
// the two game products the `if` already took; MSVC builds its compare tree
// over all seven values, and without them the pivot moves.
void sub_44AEC0(const char *pszName, const char *pszRecord, int nFlags,
                int nStyle, int nArg6, BYTE *pBits, int nNumber,
                tagDRAWITEMSTRUCT *pDraw)
{
  bn_textmetric_t tm;
  int nIcon;
  int nWidth;

  GetTextMetricsA(pDraw->hDC, &tm);
  int nHeight = tm.tmHeight;
  if (nFlags != 0) {
    if (nFlags & 0x20)
      nIcon = 5;
    else if (nFlags & 1)
      nIcon = 0;
    else if (nFlags & 2)
      nIcon = 2;
    else if (nFlags & 4)
      nIcon = 3;
    else if (nFlags & 8)
      nIcon = 1;
    else
      goto product;
    goto icon;
  }
product:
  if (pBits != NULL) {
    nWidth = sub_44B080(pDraw, pBits, nNumber, nHeight);
    goto text;
  }
  {
    DWORD dwProduct = *reinterpret_cast<const DWORD *>(pszRecord);
    int nOut1 = 0;
    int nOut2 = 0;

    glubn_get_stats(const_cast<char *>(pszRecord), 0, 0, 0, 0, &nOut1, reinterpret_cast<intptr_t>(&nOut2));
    if (dwProduct == 0x53544152 || dwProduct == 0x53455850) {
      nWidth = sub_44B210(pDraw, pszRecord, nHeight);
      goto text;
    }
    switch (dwProduct) {
    case 0x53544152:
    case 0x53455850:
      nWidth = sub_44B210(pDraw, pszRecord, nHeight);
      goto text;
    case 0x44545354:
    case 0x4452544C:
    case 0x44534852:
      nIcon = 7;
      break;
    case 0x43484154:
      nIcon = 6;
      break;
    case 0x53534852:
      nIcon = 0x17;
      break;
    default:
      nWidth = bn_icons_get_size(6)->cx;
      goto text;
    }
  }
icon:
  {
    int nY = (nHeight - bn_icons_get_size(nIcon)->cy) / 2 + 1;
    nWidth = bn_icons_get_size(nIcon)->cx;
    sub_44DFB0(pDraw->hwndItem, pDraw->rcItem.left, pDraw->rcItem.top + nY, nIcon);
  }
text:
  if (nStyle & 1) {
    RECT rc = pDraw->rcItem;
    rc.left += nWidth + 2;
    ExtTextOutA(pDraw->hDC, rc.left + 1, rc.top, 6, &rc, pszName, (UINT)strlen(pszName), NULL);
  }
}

// Stamps up to four digits right to left into the scratch buffer, shadow row
// first. Inlined into both painters. The digit is written as `nNumber % 10`
// twice, not through a local: that ranks the quotient above the counter in
// sub_44B210.
static __forceinline void bn_chat_draw_digits(int nNumber)
{
  for (int i = 4; i > 0; i--) {
    if (nNumber != 0) {
      STransBlt(dword_667308, (dword_667314 + 1) * i + 3, 6, dword_66730C, sgTransNum[nNumber % 10 + 10]);
      STransBlt(dword_667308, (dword_667314 + 1) * i + 2, 5, dword_66730C, sgTransNum[nNumber % 10]);
      nNumber /= 10;
    }
  }
}

// 0x44B080 (386 B). Paints the channel's user count: icon 0 as the backdrop,
// then up to four digits right to left, shadow row first, and blits the strip
// into the row. Returns icon 0's width. The two rectangles sit in separate
// blocks so they share one stack slot, as the oracle's do.
int sub_44B080(tagDRAWITEMSTRUCT *pDraw, BYTE *pBits, int nNumber, int nHeight)
{
  {
    blt_rect_t rcDst;
    blt_size_t sizeDst;
    blt_rect_t rcSrc;
    blt_size_t sizeSrc;

    sizeDst.cx = bn_icons_get_size(0)->cx;
    sizeDst.cy = bn_icons_get_size(0)->cy;
    rcDst.nLeft = 0;
    rcDst.nTop = 0;
    rcDst.nRight = sizeDst.cx;
    rcDst.nBottom = sizeDst.cy;
    sizeSrc.cx = bn_icons_get_size(0)->cx;
    sizeSrc.cy = bn_icons_get_size(0)->cy;
    rcSrc.nLeft = 0;
    rcSrc.nTop = 0;
    rcSrc.nRight = sizeSrc.cx;
    rcSrc.nBottom = sizeSrc.cy;
    SBltROP3Clipped(reinterpret_cast<DWORD_PTR>(dword_667308),
                    reinterpret_cast<DWORD_PTR>(&rcDst),
                    reinterpret_cast<DWORD_PTR>(&sizeDst), sizeDst.cx,
                    reinterpret_cast<DWORD_PTR>(pBits), &rcSrc, &sizeSrc,
                    sizeSrc.cx, 0, 0xCC0020);
  }
  if (nNumber != 0)
    bn_chat_draw_digits(nNumber);
  int nY = (nHeight - bn_icons_get_size(0)->cy) / 2 + 1;
  {
    blt_rect_t rcWin;

    rcWin.nLeft = 0;
    rcWin.nTop = 0;
    rcWin.nRight = dword_66730C;
    rcWin.nBottom = dword_667310;
    SDlgBltToWindowE(reinterpret_cast<DWORD_PTR>(pDraw->hwndItem), pDraw->rcItem.left,
                     pDraw->rcItem.top + nY, reinterpret_cast<DWORD_PTR>(dword_667308),
                     &rcWin, &dword_66730C, 0xFFFFFFFF);
  }
  return bn_icons_get_size(0)->cx;
}

// 0x44B210 (466 B). Paints one row's status icon out of its record: away,
// operator, squelched or a ping bar, and the user count over it when there is
// one. Returns the icon's width.
int sub_44B210(tagDRAWITEMSTRUCT *pDraw, const char *pszRecord, int nHeight)
{
  int nNumber = 0;
  int nFlags = 0;
  int nPing = 0;
  int bAway = 0;
  int nIcon;
  blt_size_t sizeDst;

  glubn_get_stats(const_cast<char *>(pszRecord), reinterpret_cast<intptr_t>(&nNumber),
             reinterpret_cast<intptr_t>(&nFlags), reinterpret_cast<intptr_t>(&nPing),
             reinterpret_cast<intptr_t>(&bAway), NULL, 0);
  if (bAway != 0)
    nIcon = 0x16;
  else if (nNumber != 0) {
    nIcon = 0x13;
    if (nFlags != 0) {
      nIcon = 0x14;
      if (nFlags == 1)
        nIcon = 0x15;
    }
  } else
    nIcon = (nPing < 10 ? nPing : 10) + 8;
  {
    blt_rect_t rcDst;

    sizeDst.cx = bn_icons_get_size(nIcon)->cx;
    sizeDst.cy = bn_icons_get_size(nIcon)->cy;
    rcDst.nLeft = 0;
    rcDst.nTop = 0;
    rcDst.nRight = sizeDst.cx;
    rcDst.nBottom = sizeDst.cy;
    sub_44DF60(dword_667308, &rcDst, &sizeDst, sizeDst.cx, nIcon);
  }
  if (nNumber != 0 && bAway == 0)
    bn_chat_draw_digits(nNumber);
  int nY = (nHeight - bn_icons_get_size(nIcon)->cy) / 2 + 1;
  {
    blt_rect_t rcWin;

    rcWin.nLeft = 0;
    rcWin.nTop = 0;
    rcWin.nRight = dword_66730C;
    rcWin.nBottom = dword_667310;
    SDlgBltToWindowE(reinterpret_cast<DWORD_PTR>(pDraw->hwndItem), pDraw->rcItem.left,
                     pDraw->rcItem.top + nY, reinterpret_cast<DWORD_PTR>(dword_667308),
                     &rcWin, &dword_66730C, 0xFFFFFFFF);
  }
  return bn_icons_get_size(nIcon)->cx;
}

// 0x44B3F0 (84 B). Frees the scratch buffer and all twenty digit masks.
void bn_chat_free()
{
  if (dword_667308 != NULL) {
    SMemFree(dword_667308, "gluBNChat.cpp", 0x28F, 0);
    dword_667308 = NULL;
  }
  for (unsigned i = 0; i < 20; i++) {
    if (sgTransNum[i] != NULL) {
      STransDelete(sgTransNum[i]);
      sgTransNum[i] = NULL;
    }
  }
}

// 0x44B450 (260 B). Cuts number.pcx into two rows of ten digit masks, then
// allocates the scratch buffer.
void bn_chat_init()
{
  void *pBits;
  blt_size_t size;
  rect32_t rc;

  LoadArtFile(NULL, NULL, "Icon", -1, 1, "glue\\battle.net\\icons\\number.pcx",
              &pBits, &size, 0);
  dword_667314 = size.cx;
  int nHeight = size.cy / 20;
  for (int i = 0; i < 10; i++) {
    rc.left = 0;
    rc.top = i * nHeight;
    rc.right = size.cx;
    rc.bottom = (i + 1) * nHeight;
    STransCreateE(pBits, size.cx, nHeight, 8, &rc, 0x1000000, &sgTransNum[i]);
    rc.top = (i + 10) * nHeight;
    rc.bottom = rc.top + nHeight;
    STransCreateE(pBits, size.cx, nHeight, 8, &rc, 0x1000000, &sgTransNum[i + 10]);
  }
  SMemFree(pBits, "gluBNChat.cpp", 0x285, 0);
  blt_size_t *pIcon = bn_icons_get_size(8);
  dword_66730C = pIcon->cx;
  dword_667310 = pIcon->cy;
  dword_667308 = static_cast<BYTE *>(SMemAlloc(dword_66730C * dword_667310, "gluBNChat.cpp", 0x289, 0));
}
