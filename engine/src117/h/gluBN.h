// gluBN.h -- declarations for src117/lang/gluBN.cpp: the battle.net glue, its
// text runs, the SNet UI callback block, and the art and dialog helpers.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_GLUBN_H
#define OC117_GLUBN_H

#include "platform.h"
#include "gluBNOkCancel.h"   // UiMessageBoxCallback
#include "TSList.h"   // the intrusive link at offset 0 of _DRAWTEXT
#include "rect32.h"   // the 16-byte block DrawTextCreate movdqu-copies

// One on-screen Battle.net text run, 0x60 bytes, named as 1.17's RTTI names it.
// DrawTextCreate (0x449AD0) is the only body that fills one in: the device
// context, the rectangle, two zeroed dwords, GetTextMetricsA's metrics, and the
// line height, metric[4] + metric[0].
struct _DRAWTEXT {
  /*0x00*/ TSLink<_DRAWTEXT> link;
  /*0x08*/ void *hDC;
  /*0x0C*/ rect32_t rc;
  /*0x1C*/ int nField1C;
  /*0x20*/ int nField20;
  /*0x24*/ LONG alTextMetric[14];
  /*0x5C*/ LONG nLineHeight;
};

OC_X86_LAYOUT(sizeof(_DRAWTEXT) == 0x60, "`push 60h` into SMemAlloc");
static_assert(offsetof(_DRAWTEXT, link) == 0x00, "mov [edi], 0 / mov [edi+4h], 0");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, hDC) == 0x08, "mov [edi+8h], ebx");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, rc) == 0x0C, "movdqu [edi+0Ch], xmm0");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, nField1C) == 0x1C, "mov [edi+1Ch], 0");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, nField20) == 0x20, "mov [edi+20h], 0");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, alTextMetric) == 0x24, "lea esi, [edi+24h] is "
                                                         "GetTextMetricsA's out-parameter");
OC_X86_LAYOUT(offsetof(_DRAWTEXT, nLineHeight) == 0x5C, "mov [edi+5Ch], ecx");

// The text-run list, spelled as the ledger spells its two members.
#pragma pack(push, 1)
template <> struct TSList<_DRAWTEXT,TSGetLink<_DRAWTEXT> > {
  /*0x00*/ DWORD _unmapped_00;
  /*0x04*/ TSLink<_DRAWTEXT> link;
  // 0x449930 (84 B). Unlinks and frees every text run.
  void Clear(void);
  // 0x449A60 (103 B), `ret 4h`. Unlinks and frees pNode; returns its successor.
  _DRAWTEXT *DeleteNode(_DRAWTEXT *pNode);
};
#pragma pack(pop)

// 0x449BD0 (24 B). Unlinks and frees one text run; 0 for a null run, else 1.
int RemoveBNText(_DRAWTEXT *pDraw);

// The SNet UI callback block, 0x5C bytes, filled in only by InitSNetUIData
// (0x44A4D0), which stores the block's own size at +0. Each slot is named for
// the callback 1.17 stores in it; the six it never writes keep an offset name.
struct SNETUIDATA {
  /*0x00*/ DWORD dwSize;
  /*0x04*/ DWORD dwVersion;
  /*0x08*/ void *hParentWnd;
  /*0x0C*/ void *pfnArt;
  /*0x10*/ void *pfnAuth;
  /*0x14*/ void *pfnCreateGame;
  /*0x18*/ void *pfnDrawDesc;
  /*0x1C*/ void *pUnwritten1C;
  /*0x20*/ void *pfnMessageBox;
  /*0x24*/ void *pfnSound;
  /*0x28*/ void *pUnwritten28;
  /*0x2C*/ void *pfnGetData;
  /*0x30*/ void *pUnwritten30;
  /*0x34*/ void *pfnCategoryList;
  /*0x38*/ void *pUnwritten38;
  /*0x3C*/ void *pfnProfile;
  /*0x40*/ void *pProfileFields;
  /*0x44*/ void *pfnProfileBitmap;
  /*0x48*/ void *pUnwritten48;
  /*0x4C*/ void *pUnwritten4C;
  /*0x50*/ void *pfnLeague;
  /*0x54*/ void *pfnValidateCertificate;
  /*0x58*/ void *pfnSetLeague;
};

OC_X86_LAYOUT(sizeof(SNETUIDATA) == 0x5C, "`push 5Ch` into memset, and the "
                                          "block's own size stored at +0");
static_assert(offsetof(SNETUIDATA, dwSize) == 0x00, "mov [esi], 5Ch");
static_assert(offsetof(SNETUIDATA, dwVersion) == 0x04, "mov [esi+4h], 1h");
static_assert(offsetof(SNETUIDATA, hParentWnd) == 0x08, "SDrawGetFrameWindow's EAX");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnArt) == 0x0C, "mov [esi+0Ch], 448FA0h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnAuth) == 0x10, "mov [esi+10h], 44A780h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnCreateGame) == 0x14, "mov [esi+14h], 44C240h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnDrawDesc) == 0x18, "mov [esi+18h], 44A8E0h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten1C) == 0x1C, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnMessageBox) == 0x20, "mov [esi+20h], 44E110h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnSound) == 0x24, "mov [esi+24h], 44A170h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten28) == 0x28, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnGetData) == 0x2C, "mov [esi+2Ch], 44A060h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten30) == 0x30, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnCategoryList) == 0x34, "mov [esi+34h], 44E060h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten38) == 0x38, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnProfile) == 0x3C, "mov [esi+3Ch], 44F200h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pProfileFields) == 0x40, "UiGetProfileFields' EAX");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnProfileBitmap) == 0x44, "mov [esi+44h], 44F190h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten48) == 0x48, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pUnwritten4C) == 0x4C, "no store reaches it");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnLeague) == 0x50, "mov [esi+50h], 447220h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnValidateCertificate) == 0x54, "mov [esi+54h], 49D460h");
OC_X86_LAYOUT(offsetof(SNETUIDATA, pfnSetLeague) == 0x58, "mov [esi+58h], 466430h");

// 0x448FA0. The SNet UI art callback, reached only through pfnArt. `ret 20h`;
// arg0 is never read, arg1 is the art id, and args 2..7 are forwarded to
// SBmpLoadImage unchanged, whose result or an explicit 0 is returned.
BOOL __stdcall UiArtCallback(int nProvider, int nArtId, void *pPalette,
                             void *pBuffer, DWORD dwBufferSize, DWORD *pdwWidth,
                             DWORD *pdwHeight, DWORD *pdwBpp);

// 0x449990. Nullary; its one caller (do_bnet_dlg) discards EAX.
void sub_449990();

// 0x449AD0. ECX is the device context, EDX the rectangle; the one consuming
// caller takes the full EAX, which is the node.
_DRAWTEXT *DrawTextCreate(void *hDC, const rect32_t *prc);

// 0x449B90. Nullary and void: it ends in a tail jump to the list's Clear.
void ClearBNText();

// The 0x3C-byte font description GetObjectA fills in, LOGFONTA's layout. Only
// the underline byte at +0x15 is named: sub_449BF0 sets it before asking for a
// second font.
struct oc_logfont_t {
  /*0x00*/ LONG alHead[5];
  /*0x14*/ BYTE bField14;
  /*0x15*/ BYTE bUnderline;
  /*0x16*/ BYTE abTail[0x26];
};

static_assert(sizeof(oc_logfont_t) == 0x3C, "`push 3Ch` into GetObjectA");
static_assert(offsetof(oc_logfont_t, alHead) == 0x00, "GetObjectA fills from the base");
static_assert(offsetof(oc_logfont_t, bField14) == 0x14, "not written by name");
static_assert(offsetof(oc_logfont_t, bUnderline) == 0x15, "mov byte ptr [ebp-37h], 1h "
                                                          "over a frame based at [ebp-4Ch]");
static_assert(offsetof(oc_logfont_t, abTail) == 0x16, "the rest of the 3Ch block");

// 0x449BF0. One ECX input, the window whose font is cloned; no caller reads
// EAX, but both early exits materialise an explicit 0 and the rest a 1.
int sub_449BF0(void *hWnd);

// 0x448F40. Two register arguments, plain `ret`, and both callers discard EAX.
// ECX is the buffer and EDX its length in bytes.
void PasswordInit(char *pszBuf, int nLen);

// 0x449FD0. One ECX input, the font to measure with; the one caller consumes
// the full EAX.
int ComputeBaseUnits(void *hFont);

// 0x44A060. Reached only through the SNet UI callback table at +0x2C. `ret 14h`
// fixes five stack words; arg0 is never read.
BOOL __stdcall UiGetDataCallback(int nArg0, DWORD dwDataId, DWORD *pdwOut,
                                 DWORD dwSize, DWORD *pdwWritten);

// 0x44A0E0. ECX is the .res image, EDX its byte count, and `ret 8h` fixes the
// type and the name. Three consuming callers take the full EAX as a pointer.
void *sub_44A0E0(const void *pRes, DWORD dwSize, int nType,
                 const wchar_t *pszName);

// 0x44A170. Reached only through the SNet UI callback table at +0x24. `ret Ch`
// fixes three stack words; only the middle one is read.
BOOL __stdcall UiSoundCallback(int nArg0, int nSound, int nArg2);

// 0x44A1A0. Nullary; the tail jmp to glues_reset_prev_pal pins void.
void do_bnet_dlg();

// The two out-shapes Storm's blitting surface hands back, declared here because
// LoadArtFile's eighth parameter is one of them.
struct blt_rect_t {
  /*0x00*/ int nLeft;
  /*0x04*/ int nTop;
  /*0x08*/ int nRight;
  /*0x0C*/ int nBottom;
};

struct blt_size_t {
  /*0x00*/ int cx;
  /*0x04*/ int cy;
};

static_assert(sizeof(blt_rect_t) == 0x10, "the [ebp-30h] and [ebp-20h] store runs");
static_assert(offsetof(blt_rect_t, nLeft)   == 0x00, "mov [ebp-30h], 0");
static_assert(offsetof(blt_rect_t, nTop)    == 0x04, "mov [ebp-2Ch], 0");
static_assert(offsetof(blt_rect_t, nRight)  == 0x08, "mov [ebp-28h], edx");
static_assert(offsetof(blt_rect_t, nBottom) == 0x0C, "mov [ebp-24h], eax");
static_assert(sizeof(blt_size_t) == 0x08, "LoadArtFile writes [ecx] and [ecx+4h]");
static_assert(offsetof(blt_size_t, cx) == 0x00, "mov dword ptr [ecx], eax");
static_assert(offsetof(blt_size_t, cy) == 0x04, "mov dword ptr [ecx+4h], eax");

// 0x44A230. ECX is the control and EDX the dialog it sits in, both forwarded to
// SDlgSetBitmapE. `ret 1Ch` fixes seven stack words; it returns 0 or 1. The
// eighth parameter is an optional two-dword out-struct, the image's size.
BOOL LoadArtFile(void *pCtrl, void *pDlg, const char *pszName, int nArg3,
                 int nArg4, const char *pszFile, void **ppBits,
                 blt_size_t *pSize, int nFlag);

// 0x44A390. ECX is either an ordinal id or a string name, EDX the parent
// window, and `ret 8h` fixes the dialog procedure and the lParam. Five callers
// read the full EAX.
int gluBN_DoModalDlg(const char *pszTemplate, void *hWndParent,
                     void *pfnDlgProc, intptr_t nParam);

// 0x44A460. ECX and EDX, no stack words, and no caller reads EAX.
void BNMessageBox(const char *pszCaption, const char *pszText);

// UiMessageBoxCallback (0x44E110) is seated in gluBNOkCancel.h, its owner.

// 0x44A490 / 0x44A6B0. Nullary; src117/lang/uiMulti.cpp declares both locally
// as `void __fastcall`, which is the same symbol under /Gr.
void glubn_free();
void glubn_init();

// 0x44A560. Six out-parameters over a space-separated record; `ret 14h` fixes
// five stack words on top of ECX and EDX. Spelled as gluBNJoin.cpp spells it:
// four `int` slots are really `int *`, cast back in the body, and widening them
// would change the decorated name the payload links against.
int glubn_get_stats(void *pArg0, intptr_t nArg1, intptr_t nArg2, intptr_t nArg3, intptr_t nArg4,
               int *pnOut, intptr_t nArg6);

// 0x44A4D0. One ECX input, the block this fills in.
void InitSNetUIData(SNETUIDATA *pUIData);

// 0x4D0100, the import thunk for Storm's ordinal 346. void * rather than HWND,
// so this header needs no <windows.h>, and spelled character for character as
// gluBNProfile.h spells it, so the two cannot become an overload.
extern "C" void *__stdcall SDrawGetFrameWindow(int nArg0);

// 0x44A700. ECX is a string id, EDX the destination buffer, and `ret 4h` fixes
// its size. The body materialises EAX explicitly on the copying exit.
int sub_44A700(int nId, char *pszBuf, DWORD cchBuf);

#endif  // OC117_GLUBN_H
