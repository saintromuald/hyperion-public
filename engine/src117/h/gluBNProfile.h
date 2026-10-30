// gluBNProfile.h -- declarations for src117/lang/gluBNProfile.cpp: the
// battle.net profile dialog, its painters, its fonts and its key table.
//
// This file owns the free functions, spelled without an explicit __fastcall,
// which /Gr already makes the default.

#ifndef OC117_GLUBNPROFILE_H
#define OC117_GLUBNPROFILE_H

// No system include here, and no header that reaches one: gen/oracle_thunks.cpp
// includes this after dlg_ctrl.h's `#undef _toupper`, and <windows.h> would
// bring the macro back. Windows handles are spelled void * and rectangles
// rect32_t, as the rest of src117/h does.
#include "platform.h"
#include "rect32.h"    // the four-long rectangle, in place of the SDK RECT
#include "gluBN.h"     // blt_rect_t, blt_size_t, _DRAWTEXT and the gluBN.cpp
                       // bodies this TU calls

// ECX and EDX live-in, `ret 8h` for the two stack words, and all three callers
// discard EAX.
void bn_blit_profile_img(void *pSurface, void *pBits, int nWidth, int nHeight);

// The WM_DRAWITEM handler for the static profile text. ECX is never read; EDX
// is the DRAWITEMSTRUCT, spelled void * because this header reaches no SDK
// type, and the definition casts it.
void sub_44E930(void *hDlg, void *pDrawItem);

// The wrapped-text painter both draw paths call. ECX and EDX go straight to
// DrawTextCreate, `ret 0Ch` fixes three stack words, and it returns 0 or 1.
BOOL sub_44E9A0(void *hDC, rect32_t *prcItem, char *pszText, const void *pPt,
                char **ppEnd);

// The two panel painters, both ECX and EDX with `ret 4h`, called on the same
// (surface, width, height) as bn_blit_profile_img. sub_44EAA0 does read EDX.
void UiProfileDrawBlank(void *pSurface, int nWidth, int nHeight);
void sub_44EAA0(void *pSurface, int nWidth, int nHeight);

// The profile dialog's window procedure, reached only by address.
intptr_t __stdcall glubn_profile_wndproc(void *hDlg, unsigned int nMsg,
                                     uintptr_t wParam, LONG_PTR lParam);

// The save hook UiProfileCallback receives and four bodies read: a __stdcall
// function pointer, not a flag.
typedef void (__stdcall *PFN_PROFILE_SAVE)(const char *pszEmpty, int nCount,
                                           char **ppArg2, char **ppArg3);

// Three more gluBN.cpp bodies gluBN.h does not declare. sub_449D30 is ECX and
// EDX plus `ret 8h`, and both callers read EAX.
intptr_t league_get_name();
void *league_load_profile_img();
int sub_449D30(_DRAWTEXT *pDraw, char *pszText, int nKind,
               rect32_t *prcHit);

// Two MBCS CRT entries, __cdecl at C linkage, spelled with `char *` as gen/'s
// table spells them: a second C-linkage prototype with other types is C2733.
extern "C" {
char *__cdecl _mbsstr(const char *psz, const char *pszFind);
char *__cdecl _mbspbrk(const char *psz, const char *pszSet);
}

// Four more Storm imports with no other home in src117/h. Storm_228 keeps the
// ledger's spelling.
extern "C" {
BOOL __stdcall SDlgSetControlBitmaps(void *hDlg, const int *pnCtrlIds,
                                     int nArg2, void *pArt, blt_size_t *pSize,
                                     int nArg5, int nArg6);
intptr_t __stdcall SDlgDefDialogProc(void *hDlg, unsigned int nMsg,
                                uintptr_t wParam, LONG_PTR lParam);
BOOL __stdcall SDlgEndDialog(void *hDlg, int nResult);
BOOL __stdcall Storm_228(void *hDlg);

// Ordinal 222, ten pushes.
BOOL __stdcall SDlgSetBitmapE(void *hCtrl, void *hDlg, const char *pszName,
                              int nArg3, int nArg4, void *pBits, rect32_t *prc,
                              int cx, int cy, int nArg9);
}

// The dialog's teardown. Nullary; its one caller discards EAX.
void sub_44F270();

// Loads the panel's two art files and skins the button. ECX only; its one
// caller discards EAX.
void sub_44EFF0(void *hDlg);

// The three handlers the window procedure calls. sub_44E6E0 takes ECX and EDX,
// the other two only ECX, and none of the three returns a value the caller
// reads.
void sub_44E6E0(void *hDlg, void *hwndItem);

// Opens the link sub_44E9A0 found under the cursor. ECX and EDX; its one caller
// discards EAX.
void sub_44F090(void *hDlg, char *pszUrl);
void sub_44F4C0(void *hDlg);   // WM_INITDIALOG
void sub_44F760(void *hDlg);

// SNet calls this one through the UI data block at +0x44. `ret 2Ch` fixes
// eleven stack words; both exits return 1.
BOOL __stdcall UiProfileBitmapCallback(int nArg0, int nArg1, int nArg2,
                                       int nArg3, void *pProfile, int nArg5,
                                       char **ppKeys, char **ppValues,
                                       void *pSurface, int nWidth,
                                       int nHeight);

// The other SNet entry, at +0x3C. `ret 28h` fixes ten stack words; it files six
// of them in globals and then runs the dialog modally.
BOOL __stdcall UiProfileCallback(int nArg0, int nArg1, void *pArg2, int nArg3,
                                 char *pszCaption, int nFieldCount, char **ppKeys,
                                 char **ppValues, PFN_PROFILE_SAVE pfnSave,
                                 int nArg9);

// Six Storm imports this TU is the first to need. Only their widths are known;
// the pointee types and the returns no site reads are spelled Storm's way.
extern "C" {
BOOL __stdcall SGdiDeleteObject(void *pFont);
BOOL __stdcall SGdiExtTextOut(void *pSurface, int nX, int nY, void *pClip,
                              DWORD dwColor, DWORD dwFlags, DWORD dwUnused,
                              const char *pszText, int cchText);
BOOL __stdcall SGdiGetTextExtent(const char *pszText, int cchText,
                                 blt_size_t *pSize);
BOOL __stdcall SGdiImportFont(void *hFont, void **ppFont);
BOOL __stdcall SGdiSelectObject(void *pFont);
BOOL __stdcall SGdiSetPitch(int nPitch);
}

// Storm ordinal 323, spelled exactly as gamedata.cpp spells it so the two
// declarations cannot disagree.
extern "C" {
BOOL __stdcall SBmpLoadImage(const char *pszFileName, void *pPalette,
                             void *pBuffer, DWORD dwBufferSize, DWORD *pdwWidth,
                             DWORD *pdwHeight, DWORD *pdwBpp);
}

// The three Win32 entries UiGetProfileFont reaches, through their import slots
// as in 1.17.
extern "C" {
__declspec(dllimport) int __stdcall MulDiv(int nNumber, int nNumerator,
                                           int nDenominator);
__declspec(dllimport) void *__stdcall CreateFontA(int cHeight, int cWidth,
    int cEscapement, int cOrientation, int cWeight, DWORD bItalic,
    DWORD bUnderline, DWORD bStrikeOut, DWORD iCharSet, DWORD iOutPrecision,
    DWORD iClipPrecision, DWORD iQuality, DWORD iPitchAndFamily,
    const char *pszFace);
__declspec(dllimport) BOOL __stdcall DeleteObject(void *hObject);
}


// Win32's LOGFONTA, restated because Storm.h defines NOGDI. sub_44FA60 uses
// lfHeight, lfWeight and lfFaceName; the rest is Microsoft's layout.
struct profile_logfont_t {
  /*0x00*/ LONG lfHeight;
  /*0x04*/ LONG lfWidth;
  /*0x08*/ LONG lfEscapement;
  /*0x0C*/ LONG lfOrientation;
  /*0x10*/ LONG lfWeight;
  /*0x14*/ BYTE lfItalic;
  /*0x15*/ BYTE lfUnderline;
  /*0x16*/ BYTE lfStrikeOut;
  /*0x17*/ BYTE lfCharSet;
  /*0x18*/ BYTE lfOutPrecision;
  /*0x19*/ BYTE lfClipPrecision;
  /*0x1A*/ BYTE lfQuality;
  /*0x1B*/ BYTE lfPitchAndFamily;
  /*0x1C*/ char lfFaceName[32];
};

static_assert(sizeof(profile_logfont_t) == 0x3C, "push 3Ch into memset and GetObjectA");
static_assert(offsetof(profile_logfont_t, lfHeight)   == 0x00, "mov [ebp-40h], eax");
static_assert(offsetof(profile_logfont_t, lfWeight)   == 0x10, "mov [ebp-30h], eax");
static_assert(offsetof(profile_logfont_t, lfFaceName) == 0x1C, "lea edx, [ebp-24h]");
static_assert(offsetof(profile_logfont_t, lfWidth)          == 0x04, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfEscapement)     == 0x08, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfOrientation)    == 0x0C, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfItalic)         == 0x14, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfUnderline)      == 0x15, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfStrikeOut)      == 0x16, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfCharSet)        == 0x17, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfOutPrecision)   == 0x18, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfClipPrecision)  == 0x19, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfQuality)        == 0x1A, "Microsoft's layout");
static_assert(offsetof(profile_logfont_t, lfPitchAndFamily) == 0x1B, "Microsoft's layout");

// Six more GDI entries behind NOGDI.
extern "C" {
__declspec(dllimport) int __stdcall GetObjectA(void *hObject, int cb, void *pv);
__declspec(dllimport) void *__stdcall CreateFontIndirectA(
    const profile_logfont_t *plf);
__declspec(dllimport) DWORD __stdcall SetTextColor(void *hdc, DWORD crColor);
__declspec(dllimport) void *__stdcall GetStockObject(int i);
__declspec(dllimport) void *__stdcall CreateCompatibleDC(void *hdc);
__declspec(dllimport) BOOL __stdcall DeleteDC(void *hdc);
}

// One row of the font table at 0x5038A4, 12 bytes.
struct profile_font_t {
  /*0x00*/ int         nPoints;
  /*0x04*/ const char *pszFace;
  /*0x08*/ int         nWeight;
};

OC_X86_LAYOUT(sizeof(profile_font_t) == 0x0C, "lea eax, [ecx+ecx*2] then *4");
static_assert(offsetof(profile_font_t, nPoints) == 0x00, "push [eax*4+5038A4h] into MulDiv");
OC_X86_LAYOUT(offsetof(profile_font_t, pszFace) == 0x04, "push [eax*4+5038A8h] as lpszFace");
OC_X86_LAYOUT(offsetof(profile_font_t, nWeight) == 0x08, "push [eax*4+5038ACh] as cWeight");

// ECX only; all three callers read EAX.
void *UiGetProfileFont(int nIndex);

// Clones the dialog's own font at one of the table's sizes and installs it on a
// zero-terminated list of control ids. ECX and EDX, `ret 4h`; both callers read
// EAX.
void *sub_44FA60(void *hDlg, int nIndex, const int *pnCtrlIds);

// One row of the 18-entry profile key table, at 0x4ECF58 (Star) and 0x4ED078
// (SEXP): the key sub_44FB40 compares, then the flags, the control id and the
// length limit sub_44F9C0 uses.
struct profile_key_t {
  /*0x00*/ const char *pszKey;
  /*0x04*/ DWORD       dwFlags;
  /*0x08*/ DWORD       nCtrlId;
  /*0x0C*/ DWORD       cchMax;
};

OC_X86_LAYOUT(sizeof(profile_key_t) == 0x10, "add edi, 10h walks one row");
static_assert(offsetof(profile_key_t, pszKey)  == 0x00, "push dword ptr [edi]");
OC_X86_LAYOUT(offsetof(profile_key_t, dwFlags) == 0x04, "test byte ptr [eax+esi*8+4h], 4h");
OC_X86_LAYOUT(offsetof(profile_key_t, nCtrlId) == 0x08, "push dword ptr [eax+esi*8+8h]");
OC_X86_LAYOUT(offsetof(profile_key_t, cchMax)  == 0x0C, "push dword ptr [eax+esi*8+0Ch]");

// The per-field callback sub_44EF20 drives: two register arguments and a stack
// word that sub_44FB40, the one 1.17 installs, never reads.
typedef void (*PFN_PROFILE_FIELD)(const char *pszKey, const char *pszValue,
                                  void *pUnused);

// ECX and EDX, `ret 8h` for the two stack words; both callers discard EAX.
void sub_44EF20(char **ppKeys, char **ppValues, PFN_PROFILE_FIELD pfnField,
                void *pUnused);

// ECX and EDX, and `ret 4h` for a stack word it never reads. Reached only by
// address.
void sub_44FB40(const char *pszKey, const char *pszValue, void *pUnused);

// The other field callback, same shape as sub_44FB40 but with the dialog in the
// stack word it does read.
void sub_44F9C0(const char *pszKey, const char *pszValue, void *hDlg);

// Nullary; both callers discard EAX.
void bn_profile_free();

// Nullary; neither caller reads EAX.
void bn_profile_init();

// Nullary; InitSNetUIData stores the result in the SNet UI data block at +0x40.
char *UiGetProfileFields();

#endif  // OC117_GLUBNPROFILE_H
