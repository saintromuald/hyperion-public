// Hyperion: not built. The Battle.net screens are gone from the port; the file
// is kept so that later changes to the reconstruction still apply. An address of
// its functions in 1.17's data goes to PortOriginalCodeReached.
//
// gluBNCreate.cpp -- the Battle.net create-game screen: the map browser, the game-type
// templates and the settings it persists. Bodies are in oracle VA order; the Storm file
// string is "gluBNCreate.cpp" at 0x4ECD20.
//
// TU membership cuts both ways: a callee defined here can be spliced into a caller here
// by /Ob2, which a cross-TU call never is, and a body whose size is exactly caller plus
// callee needs its callee here. __declspec(noinline) on the declaration is the lever.

#include "oc_data.h"  // Hyperion: OC_VA
#include <intrin.h>
#include "../h/Storm.h"        // SMemFree, SRegSaveString, SRegLoadString; pulls <windows.h>
#include "../h/gluBNCreate.h"
#include "../h/gluBN.h"       // glubn_get_stats, the one declaration of it we own
#include "../h/gluBNIcon.h"   // bn_icons_get_size, sub_44DFB0
#include "../h/gluMap.h"      // ClearMapList, sub_45B850
#include "../h/replay.h"      // map_free
#include "../h/gametype.h"     // TGAMEPARAMS, TEMPLATEPROC, gametype_template_allowed
#include "../h/glues.h"        // glues_get_str
#include "../gen/globals117.h"

#include <string.h>            // _stricmp, strcpy, memset
#include <stdio.h>             // fopen, fclose

// ---------------------------------------------------------------------------
// The USER32 and GDI32 entry points come from <windows.h>, which src117/h/
// Storm.h already puts on the include path. Every call site below is
// `call dword ptr [0x4EBxxx]`, 6 B through the IAT slot, which is what the
// SDK's own DECLSPEC_IMPORT emits; a local redeclaration would be a C2733.
//   0x4EB588 SendMessageA   0x4EB5DC GetParent      0x4EB5E8 OffsetRect
//   0x4EB5F4 GetWindowRect  0x4EB600 InvalidateRect 0x4EB61C ShowWindow
//   0x4EB620 ReleaseDC      0x4EB624 GetDC          0x4EB63C GetDesktopWindow
//   0x4EB060 CreateCompatibleDC  0x4EB084 GetTextMetricsA
//   0x4EB08C SelectObject        0x4EB0A4 DeleteDC
//
// The four GDI names are the exception: src117/h/Storm.h defines NOGDI before
// <windows.h>, so wingdi.h never declares them and they are spelled here.
extern "C" __declspec(dllimport) HDC     __stdcall CreateCompatibleDC(HDC hdc);
extern "C" __declspec(dllimport) BOOL    __stdcall GetTextMetricsA(HDC hdc, void *lptm);
extern "C" __declspec(dllimport) HGDIOBJ __stdcall SelectObject(HDC hdc, HGDIOBJ h);
extern "C" __declspec(dllimport) BOOL    __stdcall DeleteDC(HDC hdc);
extern "C" __declspec(dllimport) int     __stdcall GetObjectA(HGDIOBJ h, int c,
                                                              void *pv);      // 0x4EB080
extern "C" __declspec(dllimport) HGDIOBJ __stdcall CreateFontIndirectA(const void *lplf);
extern "C" __declspec(dllimport) COLORREF __stdcall SetTextColor(HDC hdc, COLORREF color);
                                                                              // 0x4EB050
extern "C" __declspec(dllimport) COLORREF __stdcall SetBkColor(HDC hdc, COLORREF color);
                                                                              // 0x4EB078
extern "C" __declspec(dllimport) BOOL    __stdcall TextOutA(HDC hdc, int x, int y,
                                                          LPCSTR lpString, int c);
                                                                              // 0x4EB06C
extern "C" __declspec(dllimport) BOOL    __stdcall GetTextExtentPoint32A(HDC hdc, LPCSTR lpString,
                                                                       int c, LPSIZE psizl);
                                                                              // 0x4EB094
extern "C" __declspec(dllimport) HGDIOBJ __stdcall GetStockObject(int i);
                                                                              // 0x4EB098
extern "C" __declspec(dllimport) BOOL    __stdcall GetTextExtentExPointA(HDC hdc, LPCSTR lpszString,
                                                                       int cchString, int nMaxExtent,
                                                                       LPINT lpnFit, LPINT lpnDx,
                                                                       LPSIZE lpSize);
                                                                              // 0x4EB090
                                                                              // 0x4EB0A8

// Two Storm imports src117/h/Storm.h does not carry yet, both 6-byte `jmp [slot]`
// stubs, so src117/storm_imports.def needs them by name. SStrCmpI is spelled as
// src117/lang/replay.cpp spells it, parameter names included, and SDrawGetFrameWindow
// comes from src117/h/gluBN.h, which owns it.
extern "C" {
// _mbsrchr is declared here rather than through <mbstring.h>: that header also
// declares _mbsicmp, which src117/h/save_dlg.h:240 already carries, and the two
// are a C2733.
unsigned char *__cdecl _mbsrchr(const unsigned char *pszStr, unsigned int ch);
int  __stdcall SStrCmpI(const char *pszA, const char *pszB, size_t cchMax);
intptr_t __stdcall SDlgDefDialogProc(void *hDlg, unsigned int nMsg,
                                uintptr_t wParam, LONG_PTR lParam);
BOOL __stdcall SDlgEndDialog(void *hDlg, int nResult);
BOOL __stdcall Storm_228(void *hDlg);
BOOL __stdcall SNetLeagueLogout(char *pszName);

BOOL __stdcall SDlgSetControlBitmaps(intptr_t hDlg, const int *pIds, int nZero,
                                     void *pBitmap, void *pRects, int nFlags,
                                     int nIndex);
}

// GDI's TEXTMETRICA, restated for the same reason. Not oracle-derived: this is
// the SDK's own layout, and only tmHeight at +0 is read.
struct TEXTMETRIC_A {
  LONG tmHeight, tmAscent, tmDescent, tmInternalLeading, tmExternalLeading;
  LONG tmAveCharWidth, tmMaxCharWidth, tmWeight, tmOverhang;
  LONG tmDigitizedAspectX, tmDigitizedAspectY;
  char tmFirstChar, tmLastChar, tmDefaultChar, tmBreakChar;
  BYTE tmItalic, tmUnderlined, tmStruckOut, tmPitchAndFamily, tmCharSet;
};
static_assert(sizeof(TEXTMETRIC_A) == 56, "sub esp,40h less the two saved dwords");

struct LOGFONT_A {
  LONG lfHeight, lfWidth, lfEscapement, lfOrientation, lfWeight;
  BYTE lfItalic, lfUnderline, lfStrikeOut, lfCharSet, lfOutPrecision;
  BYTE lfClipPrecision, lfQuality, lfPitchAndFamily;
  char lfFaceName[32];
};
static_assert(sizeof(LOGFONT_A) == 0x3C, "GetObjectA is called with 3Ch");
static_assert(offsetof(LOGFONT_A, lfWeight) == 0x10, "mov [ebp-30h], 190h");
static_assert(offsetof(LOGFONT_A, lfFaceName) == 0x1C, "the strcpy lands at [ebp-24h]");

// ---------------------------------------------------------------------------
// Globals this TU reaches, dual-spelled. One #ifdef per name, never one over
// the block: gen emits only marked rows' globals, so the set grows mid-wave and
// a single guard would undeclare every name under it. None of these carries a
// type in re/globals117.json, so each name below is the spelling
// sc-refs/sheets/data_naming_1.17.csv already files under gluBNCreate.cpp.

// 0x66732C -- pushed as an LB_INSERTSTRING lParam, so a string; the next named
// address is 0x667330, which caps it at four bytes.
#ifndef OC_BYTE_66732C
#define OC_BYTE_66732C
#ifdef OC_DLL_MODE
#define byte_66732C (*reinterpret_cast<char (*)[4]>(OC_VA(0x66732Cu)))
#else
extern char byte_66732C[4];
#endif
#endif

// 0x667318 / 0x66731C / 0x667320 / 0x667324 -- four owned allocations, each
// tested against 0 and released through SMemFree.
#ifndef OC_DWORD_667318
#define OC_DWORD_667318
#ifdef OC_DLL_MODE
#define dword_667318 (*reinterpret_cast<void **>(OC_VA(0x667318u)))
#else
extern void *dword_667318;
#endif
#endif

#ifndef OC_DWORD_66731C
#define OC_DWORD_66731C
#ifdef OC_DLL_MODE
#define dword_66731C (*reinterpret_cast<void **>(OC_VA(0x66731Cu)))
#else
extern void *dword_66731C;
#endif
#endif

#ifndef OC_DWORD_667320
#define OC_DWORD_667320
#ifdef OC_DLL_MODE
#define dword_667320 (*reinterpret_cast<void **>(OC_VA(0x667320u)))
#else
extern void *dword_667320;
#endif
#endif

#ifndef OC_DWORD_667324
#define OC_DWORD_667324
#ifdef OC_DLL_MODE
#define dword_667324 (*reinterpret_cast<void **>(OC_VA(0x667324u)))
#else
extern void *dword_667324;
#endif
#endif

#ifndef OC_DWORD_667328
#define OC_DWORD_667328
#ifdef OC_DLL_MODE
#define dword_667328 (*reinterpret_cast<void **>(OC_VA(0x667328u)))
#else
extern void *dword_667328;
#endif
#endif

// 0x667330 / 0x667334 -- two flags, both read as full dwords against 0.
#ifndef OC_DWORD_667330
#define OC_DWORD_667330
#ifdef OC_DLL_MODE
#define dword_667330 (*reinterpret_cast<int *>(OC_VA(0x667330u)))
#else
extern int dword_667330;
#endif
#endif

#ifndef OC_DWORD_667334
#define OC_DWORD_667334
#ifdef OC_DLL_MODE
#define dword_667334 (*reinterpret_cast<int *>(OC_VA(0x667334u)))
#else
extern int dword_667334;
#endif
#endif

// 0x667350 / 0x667354 / 0x667358 -- the selected game category, subcategory and
// type. All three are stored as dwords and read narrower at other sites
// (`mov cl, byte ptr [0x667350]`, `movzx eax, word ptr [0x667358]`).
#ifndef OC_DWORD_667350
#define OC_DWORD_667350
#ifdef OC_DLL_MODE
#define dword_667350 (*reinterpret_cast<intptr_t *>(OC_VA(0x667350u)))
#else
extern intptr_t dword_667350;
#endif
#endif

#ifndef OC_DWORD_667354
#define OC_DWORD_667354
#ifdef OC_DLL_MODE
#define dword_667354 (*reinterpret_cast<int *>(OC_VA(0x667354u)))
#else
extern int dword_667354;
#endif
#endif

#ifndef OC_DWORD_667358
#define OC_DWORD_667358
#ifdef OC_DLL_MODE
#define dword_667358 (*reinterpret_cast<intptr_t *>(OC_VA(0x667358u)))
#else
extern intptr_t dword_667358;
#endif
#endif

// 0x667360 -- the game-list control's window handle.
#ifndef OC_DWORD_667360
#define OC_DWORD_667360
#ifdef OC_DLL_MODE
#define dword_667360 (*reinterpret_cast<void **>(OC_VA(0x667360u)))
#else
extern void *dword_667360;
#endif
#endif

// 0x6677D0 / 0x6678D0 -- the two control texts the browse dialog saves with
// WM_GETTEXT, 0x100 each; sub_44DEA0 returns the first.
#ifndef OC_BYTE_6677D0
#define OC_BYTE_6677D0
#ifdef OC_DLL_MODE
#define byte_6677D0 (*reinterpret_cast<char (*)[0x100]>(OC_VA(0x6677D0u)))
#else
extern char byte_6677D0[0x100];
#endif
#endif

#ifndef OC_BYTE_6678D0
#define OC_BYTE_6678D0
#ifdef OC_DLL_MODE
#define byte_6678D0 (*reinterpret_cast<char (*)[0x100]>(OC_VA(0x6678D0u)))
#else
extern char byte_6678D0[0x100];
#endif
#endif

// 0x4ECC40 -- the game-speed table, read as `movsx ecx, word ptr [eax*4+...]`
// so its first field is a signed 16-bit string id and its stride is 4. Seven
// entries: the .rdata path string at 0x4ECC5C is the next object.
struct GAMESPEED {
  /*0x00*/ short wStrId;
  /*0x02*/ short wSpeed;
};
static_assert(sizeof(GAMESPEED) == 4, "the eax*4 stride");
static_assert(offsetof(GAMESPEED, wStrId) == 0, "movsx word ptr [eax*4+4ECC40h]");
static_assert(offsetof(GAMESPEED, wSpeed) == 2, "the second word of each pair");

#ifndef OC_STRU_4ECC40
#define OC_STRU_4ECC40
#ifdef OC_DLL_MODE
#define stru_4ECC40 (*reinterpret_cast<const GAMESPEED (*)[7]>(OC_VA(0x4ECC40u)))
#else
extern const GAMESPEED stru_4ECC40[7];
#endif
#endif

// 0x503400 -- one {point size, face name} pair per dialog font, indexed with a
// stride of 8 (`[edi*8+503400h]` and `[edi*8+503404h]`).
struct DLGFONT {
  /*0x00*/ int nPoints;
  /*0x04*/ const char *pszFace;
};
OC_X86_LAYOUT(sizeof(DLGFONT) == 8, "the edi*8 stride");
static_assert(offsetof(DLGFONT, nPoints) == 0, "push dword ptr [edi*8+503400h]");
OC_X86_LAYOUT(offsetof(DLGFONT, pszFace) == 4, "mov ecx, dword ptr [edi*8+503404h]");

#ifndef OC_STRU_503400
#define OC_STRU_503400
#ifdef OC_DLL_MODE
#define stru_503400 (reinterpret_cast<const DLGFONT *>(OC_VA(0x503400u)))
#else
extern const DLGFONT stru_503400[];
#endif
#endif

// 0x5033F0 -- the maps root. Loaded as a dword and handed to SStrCopy's source,
// so a pointer, not the buffer.
#ifndef OC_SGBNMAPSDIR_G
#define OC_SGBNMAPSDIR_G
#ifdef OC_DLL_MODE
#define sgBNMapsDir_g (*reinterpret_cast<const char **>(OC_VA(0x5033F0u)))
#else
extern const char *sgBNMapsDir_g;
#endif
#endif

// 0x5033F4 -- one art path per icon slot, indexed with a stride of 4.
#ifndef OC_OFF_5033F4
#define OC_OFF_5033F4
#ifdef OC_DLL_MODE
#define off_5033F4 (reinterpret_cast<const char *const *>(OC_VA(0x5033F4u)))
#else
extern const char *const off_5033F4[];
#endif
#endif

// 0x503418 -- the scratch file the shell association probe writes and deletes.
// Loaded as a dword at every site, so a pointer.
#ifndef OC_OFF_503418
#define OC_OFF_503418
#ifdef OC_DLL_MODE
#define off_503418 (*reinterpret_cast<const char **>(OC_VA(0x503418u)))
#else
extern const char *off_503418;
#endif
#endif

// 0x5033FC -- the speed table's live length. Read only as `cmp ax, word ptr`
// and `movsx eax, word ptr`, so a signed 16-bit count; the sheet's placeholder
// spelling `dword_5033FC` disagrees and the oracle's width wins.
#ifndef OC_WORD_5033FC
#define OC_WORD_5033FC
#ifdef OC_DLL_MODE
#define word_5033FC (*reinterpret_cast<short *>(OC_VA(0x5033FCu)))
#else
extern short word_5033FC;
#endif
#endif

// 0x667364 / 0x667368 -- two more create-screen control handles the browse
// dialog disables and re-enables.
#ifndef OC_DWORD_667364
#define OC_DWORD_667364
#ifdef OC_DLL_MODE
#define dword_667364 (*reinterpret_cast<void **>(OC_VA(0x667364u)))
#else
extern void *dword_667364;
#endif
#endif

#ifndef OC_DWORD_667368
#define OC_DWORD_667368
#ifdef OC_DLL_MODE
#define dword_667368 (*reinterpret_cast<void **>(OC_VA(0x667368u)))
#else
extern void *dword_667368;
#endif
#endif

// 0x66735C -- the game-type combo box's window handle.
#ifndef OC_DWORD_66735C
#define OC_DWORD_66735C
#ifdef OC_DLL_MODE
#define dword_66735C (*reinterpret_cast<void **>(OC_VA(0x66735Cu)))
#else
extern void *dword_66735C;
#endif
#endif

// 0x6675BC -- the id gametype_template_allowed filters templates against, whose second
// parameter src117/h/gametype.h derived as `int`.
#ifndef OC_DWORD_6675BC
#define OC_DWORD_6675BC
#ifdef OC_DLL_MODE
#define dword_6675BC (*reinterpret_cast<intptr_t *>(OC_VA(0x6675BCu)))
#else
extern intptr_t dword_6675BC;
#endif
#endif

// 0x66736C / 0x667370 / 0x667374 / 0x667378 / 0x66737C / 0x667380 / 0x667384
// -- the create-game option controls, each only ever a ShowWindow or
// SendMessageA window handle.
#ifndef OC_DWORD_66736C
#define OC_DWORD_66736C
#ifdef OC_DLL_MODE
#define dword_66736C (*reinterpret_cast<void **>(OC_VA(0x66736Cu)))
#else
extern void *dword_66736C;
#endif
#endif

#ifndef OC_DWORD_667370
#define OC_DWORD_667370
#ifdef OC_DLL_MODE
#define dword_667370 (*reinterpret_cast<void **>(OC_VA(0x667370u)))
#else
extern void *dword_667370;
#endif
#endif

#ifndef OC_DWORD_667374
#define OC_DWORD_667374
#ifdef OC_DLL_MODE
#define dword_667374 (*reinterpret_cast<void **>(OC_VA(0x667374u)))
#else
extern void *dword_667374;
#endif
#endif

#ifndef OC_DWORD_667378
#define OC_DWORD_667378
#ifdef OC_DLL_MODE
#define dword_667378 (*reinterpret_cast<void **>(OC_VA(0x667378u)))
#else
extern void *dword_667378;
#endif
#endif

#ifndef OC_DWORD_66737C
#define OC_DWORD_66737C
#ifdef OC_DLL_MODE
#define dword_66737C (*reinterpret_cast<void **>(OC_VA(0x66737Cu)))
#else
extern void *dword_66737C;
#endif
#endif

#ifndef OC_DWORD_667380
#define OC_DWORD_667380
#ifdef OC_DLL_MODE
#define dword_667380 (*reinterpret_cast<void **>(OC_VA(0x667380u)))
#else
extern void *dword_667380;
#endif
#endif

#ifndef OC_DWORD_667384
#define OC_DWORD_667384
#ifdef OC_DLL_MODE
#define dword_667384 (*reinterpret_cast<void **>(OC_VA(0x667384u)))
#else
extern void *dword_667384;
#endif
#endif

// 0x667338 / 0x667340 / 0x667348 -- one blt_size_t per loaded bitmap, passed by
// address to LoadArtFile's eighth parameter and to SDlgSetControlBitmaps. The
// next named address after each caps it at eight bytes.
#ifndef OC_STRU_667338
#define OC_STRU_667338
#ifdef OC_DLL_MODE
#define stru_667338 (*reinterpret_cast<blt_size_t *>(OC_VA(0x667338u)))
#else
extern blt_size_t stru_667338;
#endif
#endif

#ifndef OC_STRU_667340
#define OC_STRU_667340
#ifdef OC_DLL_MODE
#define stru_667340 (*reinterpret_cast<blt_size_t *>(OC_VA(0x667340u)))
#else
extern blt_size_t stru_667340;
#endif
#endif

#ifndef OC_STRU_667348
#define OC_STRU_667348
#ifdef OC_DLL_MODE
#define stru_667348 (*reinterpret_cast<blt_size_t *>(OC_VA(0x667348u)))
#else
extern blt_size_t stru_667348;
#endif
#endif

// 0x667388 / 0x667398 / 0x6673A8 -- the create-game dialog clears the first
// two as sixteen bytes each and sets the byte after them to 4; nothing else in
// the oracle names any of the three by address.
#ifndef OC_XMMWORD_667388
#define OC_XMMWORD_667388
#ifdef OC_DLL_MODE
#define xmmword_667388 (*reinterpret_cast<BYTE (*)[0x10]>(OC_VA(0x667388u)))
#else
extern BYTE xmmword_667388[0x10];
#endif
#endif

#ifndef OC_XMMWORD_667398
#define OC_XMMWORD_667398
#ifdef OC_DLL_MODE
#define xmmword_667398 (*reinterpret_cast<BYTE (*)[0x10]>(OC_VA(0x667398u)))
#else
extern BYTE xmmword_667398[0x10];
#endif
#endif

#ifndef OC_BYTE_6673A8
#define OC_BYTE_6673A8
#ifdef OC_DLL_MODE
#define byte_6673A8 (*reinterpret_cast<BYTE *>(OC_VA(0x6673A8u)))
#else
extern BYTE byte_6673A8;
#endif
#endif

// 0x50341C / 0x503450 (create dialog) and 0x50347C / 0x5034B0 (browse dialog)
// -- zero-terminated control-id lists for sub_44DBE0's two font sizes; each
// runs up to the next named address.
#ifndef OC_DWORD_50341C
#define OC_DWORD_50341C
#ifdef OC_DLL_MODE
#define dword_50341C (*reinterpret_cast<int (*)[13]>(OC_VA(0x50341Cu)))
#else
extern int dword_50341C[13];
#endif
#endif

#ifndef OC_DWORD_503450
#define OC_DWORD_503450
#ifdef OC_DLL_MODE
#define dword_503450 (*reinterpret_cast<int (*)[11]>(OC_VA(0x503450u)))
#else
extern int dword_503450[11];
#endif
#endif

#ifndef OC_DWORD_50347C
#define OC_DWORD_50347C
#ifdef OC_DLL_MODE
#define dword_50347C (*reinterpret_cast<int (*)[13]>(OC_VA(0x50347Cu)))
#else
extern int dword_50347C[13];
#endif
#endif

#ifndef OC_DWORD_5034B0
#define OC_DWORD_5034B0
#ifdef OC_DLL_MODE
#define dword_5034B0 (*reinterpret_cast<int (*)[4]>(OC_VA(0x5034B0u)))
#else
extern int dword_5034B0[4];
#endif
#endif

// 0x6673B0 -- the directory the browse list is showing. SStrCopy's capacity at
// 0x44C240 is 0x104; the next named address is 0x6674B8, four bytes further on.
#ifndef OC_SGBNCURRENTDIR_G
#define OC_SGBNCURRENTDIR_G
#ifdef OC_DLL_MODE
#define sgBNCurrentDir_g (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x6673B0u)))
#else
extern char sgBNCurrentDir_g[0x104];
#endif
#endif

// 0x6674B8 -- the browse filter, passed as a pointer when its first byte is
// non-zero and as null otherwise.
#ifndef OC_BYTE_6674B8
#define OC_BYTE_6674B8
#ifdef OC_DLL_MODE
#define byte_6674B8 (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x6674B8u)))
#else
extern char byte_6674B8[0x104];
#endif
#endif

// 0x536720 -- the expansion flag, only ever compared against zero as a byte.
// Registered positionally in re/globals117.json with no name; the spelling here
// is the placeholder form, not a coined name.
#ifndef OC_BYTE_536720
#define OC_BYTE_536720
#ifdef OC_DLL_MODE
#define byte_536720 (*reinterpret_cast<BYTE *>(OC_VA(0x536720u)))
#else
extern BYTE byte_536720;
#endif
#endif

// 0x6674B4 -- set to 1 when the account may create a game, 0 otherwise.
#ifndef OC_DWORD_6674B4
#define OC_DWORD_6674B4
#ifdef OC_DLL_MODE
#define dword_6674B4 (*reinterpret_cast<int *>(OC_VA(0x6674B4u)))
#else
extern int dword_6674B4;
#endif
#endif

// 0x6675C0 -- the buffer LB_GETTEXT fills. Capped at 0x104 by the next named
// address, 0x6676C4.
#ifndef OC_BYTE_6675C0
#define OC_BYTE_6675C0
#ifdef OC_DLL_MODE
#define byte_6675C0 (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x6675C0u)))
#else
extern char byte_6675C0[0x104];
#endif
#endif

// 0x6676C4 -- the subtype list's cap, compared both against -1 and, unsigned,
// against a template's slot count.
#ifndef OC_DWORD_6676C4
#define OC_DWORD_6676C4
#ifdef OC_DLL_MODE
#define dword_6676C4 (*reinterpret_cast<int *>(OC_VA(0x6676C4u)))
#else
extern int dword_6676C4;
#endif
#endif

// 0x6676C8 -- the directory the browse list should return to. Capped at 0x108
// by the next named address, 0x6677D0.
#ifndef OC_BYTE_6676C8
#define OC_BYTE_6676C8
#ifdef OC_DLL_MODE
#define byte_6676C8 (*reinterpret_cast<char (*)[0x104]>(OC_VA(0x6676C8u)))
#else
extern char byte_6676C8[0x104];
#endif
#endif

// 0x6728D0 -- gGameHeader, dual-spelled as gametype.cpp does; not registered
// because it is a struct.
#ifndef gGameHeader
#ifdef OC_DLL_MODE
#define gGameHeader (*reinterpret_cast<GameHeader *>(OC_VA(0x6728D0u)))
#else
extern GameHeader gGameHeader;
#endif
#endif

// 0x6679D0 -- the game-type caption: the parameter name plus ":", shown only
// when it is non-empty. The next named address, 0x667AD0, caps it at 0x100.
#ifndef OC_BYTE_6679D0
#define OC_BYTE_6679D0
#ifdef OC_DLL_MODE
#define byte_6679D0 (*reinterpret_cast<char (*)[0x100]>(OC_VA(0x6679D0u)))
#else
extern char byte_6679D0[0x100];
#endif
#endif

// 0x6677CC -- the selected template's map path, set by sub_44C380 and opened
// by sub_44D9D0.
#ifndef OC_DWORD_6677CC
#define OC_DWORD_6677CC
#ifdef OC_DLL_MODE
#define dword_6677CC (*reinterpret_cast<char **>(OC_VA(0x6677CCu)))
#else
extern char *dword_6677CC;
#endif
#endif

// 0x667AD0 -- the selected map entry, handed straight to sub_45B970 and to
// CreateMapInfo's ECX.
#ifndef OC_DWORD_667AD0
#define OC_DWORD_667AD0
#ifdef OC_DLL_MODE
#define dword_667AD0 (*reinterpret_cast<void **>(OC_VA(0x667AD0u)))
#else
extern void *dword_667AD0;
#endif
#endif

// 0x667ADC -- one blt_size_t per icon slot, the second half of the pair
// dword_667AD4 starts. Two entries: the walk stops at 0x667AEC.
#ifndef OC_STRU_667ADC
#define OC_STRU_667ADC
#ifdef OC_DLL_MODE
#define stru_667ADC (*reinterpret_cast<blt_size_t (*)[2]>(OC_VA(0x667ADCu)))
#else
extern blt_size_t stru_667ADC[2];
#endif
#endif

// 0x667AEC -- set once, the first time the create dialog runs.
#ifndef OC_DWORD_667AEC
#define OC_DWORD_667AEC
#ifdef OC_DLL_MODE
#define dword_667AEC (*reinterpret_cast<int *>(OC_VA(0x667AECu)))
#else
extern int dword_667AEC;
#endif
#endif

// 0x667AD4 -- two more owned allocations, walked as one array by sub_44CF10
// (`mov esi, 667AD4h` ... `cmp esi, 667ADCh`).
#ifndef OC_DWORD_667AD4
#define OC_DWORD_667AD4
#ifdef OC_DLL_MODE
#define dword_667AD4 (*reinterpret_cast<void *(*)[2]>(OC_VA(0x667AD4u)))
#else
extern void *dword_667AD4[2];
#endif
#endif

// ---------------------------------------------------------------------------
// The bodies, in oracle VA order.

// Add one browse-list entry and hang its item data off the new index.
int __stdcall sub_44B560(int nItemData, int nText, int nUnused)
{
  int nIndex = (int)SendMessageA(static_cast<HWND>(dword_667360), 0x180, 0, nText);

  SendMessageA(static_cast<HWND>(dword_667360), 0x19A, nIndex, nItemData);
  return nIndex;
}

// Add one browse-list entry, inserting a second placeholder row under it when
// the caller's flag bit is clear.
int __stdcall sub_44B5A0(int nItemData, int nText, int nFlags)
{
  int nIndex;

  if (nFlags & 4) {
    nIndex = (int)SendMessageA(static_cast<HWND>(dword_667360), 0x143, 0, nText);
    SendMessageA(static_cast<HWND>(dword_667360), 0x151, nIndex, nItemData);
    return nIndex;
  }
  nIndex = (int)SendMessageA(static_cast<HWND>(dword_667360), 0x14A, 0, nText);
  SendMessageA(static_cast<HWND>(dword_667360), 0x14A, nIndex + 1,
               reinterpret_cast<LPARAM>(byte_66732C));
  SendMessageA(static_cast<HWND>(dword_667360), 0x151, nIndex, nItemData);
  return nIndex;
}

// List one league subtype, skipping the ones the current cap excludes. A
// TEMPLATEPROC. The slot count at TGAMEPARAMS+4 has no name in gametype.h yet.
void __stdcall sub_44B610(int nArg0, int nArg1, intptr_t nArg2, int nArg3)
{
  char szName[0x100];
  const TGAMEPARAMS *const pParams = reinterpret_cast<const TGAMEPARAMS *>(nArg2);
  const unsigned nSlots =
      pParams->dwSubValue;

  if (nSlots == 0)
    return;
  if (dword_667350 == 0x0F &&
      (dword_6676C4 < 2 || nSlots >= static_cast<unsigned>(dword_6676C4)))
    return;
  gametype_get_subtype_name(szName, sizeof(szName), pParams, dword_6676C4);

  LONG nIndex = (LONG)SendMessageA(static_cast<HWND>(dword_667374), 0x143, 0,
                             reinterpret_cast<LPARAM>(szName));
  SendMessageA(static_cast<HWND>(dword_667374), 0x151, nIndex, pParams->wType);
}

// List one template whose id matches the pending filter, keyed by its
// category and subcategory. A TEMPLATEPROC.
void __stdcall sub_44B690(int nArg0, int nArg1, intptr_t nArg2, int nArg3)
{
  const TGAMEPARAMS *const pParams = reinterpret_cast<const TGAMEPARAMS *>(nArg2);

  if (gametype_template_allowed(reinterpret_cast<const BYTE *>(pParams), (int)dword_6675BC)) {
    LONG nIndex = (LONG)SendMessageA(static_cast<HWND>(dword_66735C), 0x143, 0, nArg0);
    SendMessageA(static_cast<HWND>(dword_66735C), 0x151, nIndex,
                 (pParams->bSub << 8) | pParams->bCategory);
  }
}

// Repaint the template caption on the two events that can change it, but only
// while the current selection is a simple template.
void __stdcall sub_44B6F0(int nEvent, intptr_t hWnd, intptr_t pszText)
{
  if (nEvent == 7) {
    if (!IsSimpleTemplate(static_cast<BYTE>(dword_667350),
                          static_cast<BYTE>(dword_667354),
                          static_cast<WORD>(dword_667358)))
      return;
  } else if (nEvent == 4) {
    if (IsSimpleTemplate(static_cast<BYTE>(dword_667350),
                         static_cast<BYTE>(dword_667354),
                         static_cast<WORD>(dword_667358)))
      return;
  }
  sub_44DCB0(hWnd, pszText);
}

// Show or hide one control. SW_SHOW is 5 and SW_HIDE is 0, which is what the
// neg/sbb/and pair computes without a branch.
void __stdcall sub_44B760(int nUnused, intptr_t hWnd, int bShow)
{
  ShowWindow(reinterpret_cast<HWND>(hWnd), bShow ? SW_SHOW : SW_HIDE);
}


// The map-browse dialog's procedure.
intptr_t __stdcall glubn_createbrowse_wndproc(void *hWnd, unsigned uMsg, uintptr_t wParam,
                                         LONG_PTR lParam)
{
  switch (uMsg)
  {
  case WM_DESTROY:
    sub_44CF10();
    ClearMapList();
    dword_667360 = 0;
    Storm_228(hWnd);
    return 0;
  case WM_DRAWITEM:
    if (wParam == 0x4B0)
    {
      sub_44D010(reinterpret_cast<intptr_t>(hWnd), reinterpret_cast<void *>(lParam));
      return 1;
    }
    if (wParam == 0x3F7)
    {
      sub_44D190(reinterpret_cast<intptr_t>(hWnd), reinterpret_cast<void *>(lParam));
      return 1;
    }
    break;
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
    SendMessageA(static_cast<HWND>(SDrawGetFrameWindow(0)), uMsg, wParam, lParam);
    break;
  case WM_INITDIALOG:
    sub_44BA20(reinterpret_cast<intptr_t>(hWnd));
    return 0;
  case WM_COMMAND:
    switch (LOWORD(wParam))
    {
    case IDOK:
      EnableWindow(reinterpret_cast<HWND>(lParam), FALSE);
      sub_49E740(0xA7);
      if (sub_44BCB0(reinterpret_cast<intptr_t>(hWnd)))
        SDlgEndDialog(hWnd, 1);
      EnableWindow(reinterpret_cast<HWND>(lParam), TRUE);
      sub_44DD20();
      return 0;
    case IDCANCEL:
      gPlayback = 0;
      map_free();
      EnableWindow(reinterpret_cast<HWND>(lParam), FALSE);
      sub_49E740(0xA7);
      SDlgEndDialog(hWnd, 0);
      return 0;
    case 0x4B0:
      if (HIWORD(wParam) == LBN_SELCHANGE)
      {
        int nIndex = (int)SendMessageA(reinterpret_cast<HWND>(lParam), LB_GETCURSEL, 0, 0);
        if (nIndex != -1)
        {
          void *pEntry = reinterpret_cast<void *>(
              SendMessageA(reinterpret_cast<HWND>(lParam), LB_GETITEMDATA, nIndex, 0));
          sub_45B970(pEntry);
          dword_667AD0 = pEntry;
        }
        SendMessageA(GetDlgItem(static_cast<HWND>(hWnd), 0x400), WM_COMMAND,
                     MAKEWPARAM(0, 1), lParam);
        return 0;
      }
      if (HIWORD(wParam) == LBN_DBLCLK)
      {
        if (sub_44BCB0(reinterpret_cast<intptr_t>(hWnd)))
          SDlgEndDialog(hWnd, 1);
        return 0;
      }
      break;
    }
    break;
  case WM_SYSCOMMAND:
    if (wParam == SC_CLOSE)
    {
      HWND hParent = GetParent(static_cast<HWND>(hWnd));
      SDlgEndDialog(hWnd, 0);
      PostMessageA(hParent, WM_SYSCOMMAND, SC_CLOSE, lParam);
      return 0;
    }
    break;
  case WM_CTLCOLORSTATIC:
    if (GetWindowLongA(reinterpret_cast<HWND>(lParam), GWL_ID) == 0x42D)
    {
      SetTextColor(reinterpret_cast<HDC>(wParam), 0xFFFF);
      return reinterpret_cast<intptr_t>(GetStockObject(5));
    }
    break;
  case WM_MOUSEWHEEL:
    SendMessageA(GetDlgItem(static_cast<HWND>(hWnd), 0x400), WM_MOUSEWHEEL, wParam, lParam);
    break;
  }
  return SDlgDefDialogProc(hWnd, uMsg, wParam, lParam);
}

// Initialise the map-browse dialog: fonts, the list's item height from its
// font, the art, the eight setting labels, the saved directory, the scroll bar
// link, and the template-dependent controls.
void sub_44BA20(intptr_t hDlg)
{
  HWND hWnd = reinterpret_cast<HWND>(hDlg);
  RECT rcList;

  sub_44DBE0(hDlg, 0, dword_50347C);
  sub_44DBE0(hDlg, 1, dword_5034B0);
  dword_667360 = GetDlgItem(hWnd, 0x4B0);
  GetClientRect(static_cast<HWND>(dword_667360), &rcList);
  int nHeight = sub_44D6E0(SendMessageA(static_cast<HWND>(dword_667360), WM_GETFONT, 0, 0));
  if (nHeight != -1)
    SendMessageA(static_cast<HWND>(dword_667360), LB_SETITEMHEIGHT, 0,
                 static_cast<WORD>(nHeight));
  sub_44D7F0(hDlg);
  CreateSetControl(3, GetDlgItem(hWnd, 0x3FB), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(0, GetDlgItem(hWnd, 0x3F7), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(1, GetDlgItem(hWnd, 0x3F3), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(4, GetDlgItem(hWnd, 0x3F5), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(5, GetDlgItem(hWnd, 0x3F6), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(7, GetDlgItem(hWnd, 0x3F5), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(6, GetDlgItem(hWnd, 0x3F9), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(9, GetDlgItem(hWnd, 0x406), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(8, GetDlgItem(hWnd, 0x42C), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  strcpy(sgBNCurrentDir_g, byte_6676C8);
  strcpy(byte_6674B8, byte_6675C0);

  HWND hScroll = GetDlgItem(hWnd, 0x400);
  HWND hList = static_cast<HWND>(dword_667360);
  if (hList)
  {
    SendMessageA(hScroll, 0x46A, 0, reinterpret_cast<LPARAM>(hList));
    SetWindowLongPtrA(hList, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(hScroll));
  }
  sub_44D520(hDlg, reinterpret_cast<intptr_t>(dword_667360));
  SendMessageA(GetDlgItem(hWnd, 0x3FB), WM_SETTEXT, 0, reinterpret_cast<LPARAM>(byte_6677D0));
  if (IsSimpleTemplate(static_cast<BYTE>(dword_667350), static_cast<BYTE>(dword_667354),
                       static_cast<WORD>(dword_667358)))
  {
    ShowWindow(GetDlgItem(hWnd, 0x3F9), SW_SHOW);
    ShowWindow(GetDlgItem(hWnd, 0x406), SW_SHOW);
  }
  else
  {
    ShowWindow(GetDlgItem(hWnd, 0x3F9), SW_HIDE);
    ShowWindow(GetDlgItem(hWnd, 0x406), SW_HIDE);
  }
  sub_44BDC0(hDlg, sgBNCurrentDir_g);
}

// Enter the directory the browse list has selected, or report why it could not
// be opened. Returns 1 only when a directory was entered.
int sub_44BCB0(intptr_t hDlg)
{
  LONG nIndex = (LONG)SendMessageA(static_cast<HWND>(dword_667360), 0x188, 0, 0);

  if (nIndex == -1)
    return 0;

  LONG_PTR nData = SendMessageA(static_cast<HWND>(dword_667360), 0x199, nIndex, 0);
  unsigned nErr = CreateNewGame(nData, 0, 0, 0, 0, sgBNCurrentDir_g);

  if (nErr == 0) {
    strcpy(byte_6676C8, sgBNCurrentDir_g);

    char *pszTail = reinterpret_cast<char *>(
        _mbsrchr(reinterpret_cast<const unsigned char *>(byte_6676C8), '\\'));

    if (pszTail != 0)
      *pszTail = 0;
    SendMessageA(static_cast<HWND>(dword_667360), 0x189, nIndex,
                 reinterpret_cast<LPARAM>(byte_6675C0));
    return 1;
  }
  if (nErr == 0x8000000B) {
    sub_44BDC0(hDlg, sgBNCurrentDir_g);
    byte_6674B8[0] = 0;
    sub_44D520(hDlg, reinterpret_cast<intptr_t>(dword_667360));
    return 0;
  }

  int nStrId;
  switch (nErr) {
  case 0x80000001: nStrId = 0x3C; break;
  case 0x80000002: nStrId = 0x3B; break;
  case 0x80000007: nStrId = 0x33; break;
  default: return 0;
  }
  UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(nStrId), "", 0);
  return 0;
}











// Show the last component of a directory in the browse dialog's path label,
// cut to fit with a trailing "..." when it is wider than the label.
// sizeText is declared in the font block: that scope is what gives the
// oracle's frame order.
int sub_44BDC0(intptr_t hDlg, const char *pszDir)
{
  char szText[0x104];
  RECT rcLabel;
  int anExtents[0x104];

  const char *pszName = strrchr(pszDir, '\\');
  if (pszName)
    ++pszName;
  else
    pszName = pszDir;
  SStrCopy(szText, pszName, sizeof(szText));
  HWND hLabel = GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x3F8);
  GetClientRect(hLabel, &rcLabel);
  HDC hdc = GetDC(hLabel);
  if (hdc)
  {
    HGDIOBJ hFont = reinterpret_cast<HGDIOBJ>(SendMessageA(hLabel, WM_GETFONT, 0, 0));
    if (hFont)
    {
      SIZE sizeText;
      HGDIOBJ hOld = SelectObject(hdc, hFont);
      SIZE sizeDots;
      GetTextExtentPoint32A(hdc, "...", 3, &sizeDots);
      GetTextExtentExPointA(hdc, szText, (int)strlen(szText), 0, NULL, anExtents,
                            &sizeText);
      if (hOld)
        SelectObject(hdc, hOld);
      int nWidth = rcLabel.right - rcLabel.left + 1;
      if (sizeText.cx >= nWidth)
      {
        int i;
        for (i = 0; i < static_cast<int>(strlen(szText)); ++i)
        {
          if (anExtents[i] > nWidth - sizeDots.cx)
            break;
        }
        strcpy(&szText[i], "...");
      }
    }
    ReleaseDC(hLabel, hdc);
  }
  SendMessageA(hLabel, WM_SETTEXT, 0, reinterpret_cast<LPARAM>(szText));
  RECT rcWindow;
  GetWindowRect(hLabel, &rcWindow);
  ScreenToClient(reinterpret_cast<HWND>(hDlg), reinterpret_cast<POINT *>(&rcWindow.left));
  ScreenToClient(reinterpret_cast<HWND>(hDlg), reinterpret_cast<POINT *>(&rcWindow.right));
  return InvalidateRect(reinterpret_cast<HWND>(hDlg), &rcWindow, TRUE);
}

// Re-enable the player-count slider for the template the caller names, and show
// or hide the two extra controls a simple template does not use. The second
// IsCompetitiveGame call discards its result; that redundancy is the oracle's.
void sub_44BF30(intptr_t hDlg, int nCategory, int nSub, int nType)
{
  const TGAMEPARAMS *pParams = gametype_find_template(
      static_cast<BYTE>(nCategory), static_cast<BYTE>(nSub), nType);
  HWND hCtl = GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x409);

  if (pParams != 0 &&
      (pParams->bFlag12 == 1 ||
       IsCompetitiveGame(pParams->bCategory, pParams->bSub))) {
    IsCompetitiveGame(pParams->bCategory, pParams->bSub);
    SendMessageA(hCtl, 0x405, 1, 6);
    SendMessageA(reinterpret_cast<HWND>(hDlg), 0x114, 0x60004,
                 reinterpret_cast<LPARAM>(hCtl));
    EnableWindow(hCtl, 0);
  } else {
    EnableWindow(hCtl, 1);
  }

  if (IsSimpleTemplate(static_cast<BYTE>(dword_667350),
                       static_cast<BYTE>(dword_667354),
                       static_cast<WORD>(dword_667358))) {
    ShowWindow(GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x3F9), 5);
    ShowWindow(GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x406), 5);
  } else {
    ShowWindow(GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x3F9), 0);
    ShowWindow(GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x406), 0);
  }

  LONG nSel = (LONG)SendDlgItemMessageA(reinterpret_cast<HWND>(hDlg), 0x3F1, 0x147, 0, 0);
  if (nSel != -1) {
    void *pEntry = reinterpret_cast<void *>(SendDlgItemMessageA(
        reinterpret_cast<HWND>(hDlg), 0x3F1, 0x150, nSel, 0));

    sub_45B970(pEntry);
    dword_667AD0 = pEntry;
  }
}


// Run the map-browse dialog over the create-game screen: disable and blank the
// screen's controls, run the modal, then refill the game list and select the
// directory the browser settled on.
void gluBNCreate_CreateBrowseDlg(void *hDlg, void *hButton)
{
  HWND hWnd = static_cast<HWND>(hDlg);

  sub_49E740(0xA7);
  EnableWindow(static_cast<HWND>(hButton), FALSE);
  EnableWindow(static_cast<HWND>(dword_667364), FALSE);
  EnableWindow(static_cast<HWND>(dword_667368), FALSE);
  EnableWindow(static_cast<HWND>(dword_667360), FALSE);
  EnableWindow(static_cast<HWND>(dword_66735C), FALSE);
  EnableWindow(static_cast<HWND>(dword_667374), FALSE);
  EnableWindow(static_cast<HWND>(dword_667384), FALSE);
  ClearMapList();
  SendMessageA(static_cast<HWND>(dword_667360), CB_RESETCONTENT, 0, 0);
  SendMessageA(static_cast<HWND>(dword_66735C), WM_GETTEXT, 0x100,
               reinterpret_cast<LPARAM>(byte_6677D0));
  SendMessageA(static_cast<HWND>(dword_667374), WM_GETTEXT, 0x100,
               reinterpret_cast<LPARAM>(byte_6678D0));
  ShowWindow(hWnd, SW_HIDE);
  dword_667330 = 1;
  int nResult = gluBN_DoModalDlg("DIALOG_CREATE_BROWSE", hWnd,
                                 reinterpret_cast<void *>(glubn_createbrowse_wndproc), 0);
  dword_667330 = 0;
  dword_667360 = GetDlgItem(hWnd, 0x3F1);
  InvalidateRect(hWnd, NULL, FALSE);
  UpdateWindow(hWnd);
  SendMessageA(static_cast<HWND>(dword_667360), WM_SETREDRAW, FALSE, 0);
  sub_44D320(reinterpret_cast<intptr_t>(hWnd), reinterpret_cast<intptr_t>(dword_667360));
  if (nResult)
  {
    int nIndex = sub_45B850(sgBNCurrentDir_g, reinterpret_cast<void *>(sub_44B5A0));
    if (nIndex != static_cast<int>(0x80000006))
    {
      SendMessageA(static_cast<HWND>(dword_667360), CB_SETCURSEL, nIndex, 0);
      SendMessageA(hWnd, WM_COMMAND, MAKEWPARAM(0x3F1, CBN_SELCHANGE),
                   reinterpret_cast<LPARAM>(dword_667360));
    }
    unsigned char *pSlash =
        _mbsrchr(reinterpret_cast<unsigned char *>(sgBNCurrentDir_g), '\\');
    if (pSlash)
      *pSlash = 0;
  }
  SendMessageA(static_cast<HWND>(dword_667360), WM_SETREDRAW, TRUE, 0);
  EnableWindow(static_cast<HWND>(hButton), TRUE);
  EnableWindow(static_cast<HWND>(dword_667364), TRUE);
  EnableWindow(static_cast<HWND>(dword_667368), TRUE);
  EnableWindow(static_cast<HWND>(dword_667374), TRUE);
  EnableWindow(static_cast<HWND>(dword_66735C), TRUE);
  EnableWindow(static_cast<HWND>(dword_667360), TRUE);
  EnableWindow(static_cast<HWND>(dword_667384), TRUE);
  SetFocus(static_cast<HWND>(dword_667360));
  ShowWindow(hWnd, SW_SHOW);
}

// Open the create-game dialog for the account the caller hands over, after
// resetting the browse directory to the maps root.
int __stdcall UiCreateGameCallback(int nArg0, int nArg1, intptr_t nInfo, intptr_t nArg3,
                                   int nArg4, intptr_t nArg5)
{
  int nCount;
  const int *const pInfo = reinterpret_cast<const int *>(nInfo);

  if (pInfo[1] != 0)
    SStrCopy(gLocalPlayerName, reinterpret_cast<const char *>(pInfo[1]), 0x19);

  const intptr_t nAccount = pInfo[2];

  nCount = 0;
  nInfo = 0;
  // glubn_get_stats writes an int through its sixth argument, into this zeroed
  // parameter slot; on x64 the slot's upper half stays 0.
  if (glubn_get_stats(reinterpret_cast<void *>(nAccount), 0, 0,
                 reinterpret_cast<intptr_t>(&nCount), 0, reinterpret_cast<int *>(&nInfo), 0)) {
    if (nInfo != 0 || nCount >= 0x0A)
      dword_6674B4 = 1;
    else
      dword_6674B4 = 0;
    dword_6675BC = nInfo;
  } else {
    dword_6674B4 = 0;
    dword_6675BC = 0;
  }

  SStrCopy(sgBNCurrentDir_g, sgBNMapsDir_g, 0x104);
  if (byte_536720 != 0)
    SStrPack(sgBNCurrentDir_g, "\\BroodWar", 0x104);
  sub_45C680(sgBNCurrentDir_g);
  if (dword_667AEC == 0) {
    dword_667AEC = 1;
    strcpy(byte_6676C8, sgBNCurrentDir_g);
  } else {
    strcpy(sgBNCurrentDir_g, byte_6676C8);
  }

  if (gluBN_DoModalDlg("DIALOG_CREATE_GAME",
                       *reinterpret_cast<void *const *>(nArg3 + 8),
                       reinterpret_cast<void *>(ChatCreateGameDialogProc), 0) == 0)
    return 0;
  *reinterpret_cast<DWORD *>(nArg5) = g_LocalStormID;
  return 1;
}












// The create dialog's WM_COMMAND handler. Returns nonzero when it handled the
// command, with the dialog result in *pResult.
int sub_44C380(intptr_t hDlg, intptr_t wParam, intptr_t lParam, int *pResult)
{
  switch (LOWORD(wParam))
  {
  case IDOK:
    sub_44CBA0(hDlg);
    *pResult = 0;
    return 1;
  case IDCANCEL:
    gPlayback = 0;
    map_free();
    sub_49E740(0xA7);
    SDlgEndDialog(reinterpret_cast<void *>(hDlg), 0);
    *pResult = 0;
    return 1;
  case 0x3ED:
    if (HIWORD(wParam) == CBN_SELCHANGE)
    {
      int nIndex = (int)SendMessageA(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
      if (nIndex != -1)
      {
        dword_667358 = SendMessageA(reinterpret_cast<HWND>(lParam), CB_GETITEMDATA, nIndex, 0);
        *pResult = 0;
        return 1;
      }
    }
    break;
  case 0x3F0:
    gluBNCreate_CreateBrowseDlg(reinterpret_cast<void *>(hDlg), reinterpret_cast<void *>(lParam));
    *pResult = 0;
    return 1;
  case 0x3F1:
    if (HIWORD(wParam) == CBN_SELCHANGE)
    {
      sub_44DE20(hDlg);
      sub_44DD20();
    }
    break;
  case 0x4B1:
    if (HIWORD(wParam) == CBN_SELCHANGE)
    {
      int nIndex = (int)SendMessageA(reinterpret_cast<HWND>(lParam), CB_GETCURSEL, 0, 0);
      if (nIndex != -1)
      {
        unsigned nData = (unsigned)SendMessageA(reinterpret_cast<HWND>(lParam), CB_GETITEMDATA, nIndex, 0);
        unsigned nSub = nData >> 8;
        dword_667358 = 1;
        gGameHeader.wSubType = 1;
        dword_667354 = nSub;
        gGameHeader.bGameTypeSub = static_cast<BYTE>(nSub);
        // Masked in place: a separate `nData & 0xFF` local emits `movzx`, not `and`.
        nData &= 0xFF;
        dword_667350 = nData;
        gGameHeader.bGameType = static_cast<BYTE>(nData);
        strcpy(byte_6679D0, gametype_find_param_name(static_cast<BYTE>(nData),
                                                     static_cast<BYTE>(nSub)));
        if (byte_6679D0[0])
          strcat(byte_6679D0, ":");
        sub_44DCB0(reinterpret_cast<intptr_t>(dword_66736C), reinterpret_cast<intptr_t>(byte_6679D0));
        sub_44D620(reinterpret_cast<intptr_t>(dword_667374), (int)dword_667350, (int)dword_667354);
        sub_44BF30(hDlg, (int)dword_667350, (int)dword_667354, (int)dword_667358);
        SetFocus(static_cast<HWND>(dword_66735C));
        *pResult = 0;
        BOOL bShow = byte_6679D0[0] != 0;
        ShowWindow(static_cast<HWND>(dword_66736C), bShow);
        ShowWindow(static_cast<HWND>(dword_667374), bShow);
        BYTE bCategory = static_cast<BYTE>(dword_667350);
        ShowWindow(static_cast<HWND>(dword_667384), SW_HIDE);
        if (sub_447E40())
        {
          dword_6677CC = gametype_find_template_meta_path(bCategory);
          if (!dword_6677CC)
            return 0;
          char *pszName = gametype_find_template_meta_name(bCategory);
          if (!pszName)
            return 0;
          SetWindowTextA(static_cast<HWND>(dword_667384), pszName);
          ShowWindow(static_cast<HWND>(dword_667384), SW_SHOWNORMAL);
        }
        if (IsWGTGame())
        {
          SendMessageA(static_cast<HWND>(dword_667378), 0x405, TRUE, 6);
          SendMessageA(reinterpret_cast<HWND>(hDlg), WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 6),
                       reinterpret_cast<LPARAM>(dword_667378));
          EnableWindow(static_cast<HWND>(dword_667378), FALSE);
        }
        else
        {
          EnableWindow(static_cast<HWND>(dword_667378), TRUE);
        }
        return 1;
      }
    }
    break;
  case 0x4B7:
    sub_49E740(0xA7);
    sub_44D9D0(dword_6677CC);
    return 1;
  }
  return 0;
}

// The create-game dialog's procedure.
intptr_t __stdcall ChatCreateGameDialogProc(intptr_t hDlg, unsigned nMsg, intptr_t wParam, intptr_t lParam)
{
  switch (nMsg)
  {
  case WM_DESTROY:
    sub_44DA90(reinterpret_cast<intptr_t>(dword_667364));
    sub_44DB90(reinterpret_cast<intptr_t>(dword_66735C));
    sub_44DB40(reinterpret_cast<intptr_t>(dword_667374));
    sub_44DAE0(reinterpret_cast<intptr_t>(dword_667378));
    sub_44CF70();
    dword_667364 = 0;
    dword_667368 = 0;
    dword_66736C = 0;
    dword_667360 = 0;
    dword_66735C = 0;
    dword_667374 = 0;
    dword_667378 = 0;
    dword_66737C = 0;
    dword_667380 = 0;
    dword_667370 = 0;
    dword_667384 = 0;
    ClearMapList();
    dword_667334 = 0;
    break;
  case WM_SYSKEYDOWN:
  case WM_SYSKEYUP:
    SendMessageA(static_cast<HWND>(SDrawGetFrameWindow(0)), nMsg, wParam, lParam);
    break;
  case WM_INITDIALOG:
    sub_44C980(hDlg);
    dword_667334 = 1;
    return 1;
  case WM_COMMAND:
  {
    int nResult = 0;
    if (sub_44C380(hDlg, wParam, lParam, &nResult))
      return nResult;
    break;
  }
  case WM_SYSCOMMAND:
    if (wParam == SC_CLOSE)
    {
      SNetLeagueLogout(gLocalPlayerName);
      HWND hParent = GetParent(reinterpret_cast<HWND>(hDlg));
      SDlgEndDialog(reinterpret_cast<void *>(hDlg), 0);
      PostMessageA(hParent, nMsg, SC_CLOSE, lParam);
      return 0;
    }
    break;
  case WM_CTLCOLORSTATIC:
    if (GetWindowLongA(reinterpret_cast<HWND>(lParam), GWL_ID) == 0x42D)
    {
      SetTextColor(reinterpret_cast<HDC>(wParam), 0xFFFF);
      return reinterpret_cast<intptr_t>(GetStockObject(5));
    }
    break;
  case WM_HSCROLL:
    if (reinterpret_cast<void *>(lParam) == dword_667378 &&
        (LOWORD(wParam) == SB_THUMBPOSITION || LOWORD(wParam) == SB_THUMBTRACK))
    {
      int nPos = (int)SendMessageA(reinterpret_cast<HWND>(lParam), 0x400, 0, 0);
      HWND hLabel = GetDlgItem(reinterpret_cast<HWND>(hDlg), 0x40A);
      RECT rcDlg;
      RECT rcLabel;
      GetWindowRect(reinterpret_cast<HWND>(hDlg), &rcDlg);
      GetWindowRect(hLabel, &rcLabel);
      OffsetRect(&rcLabel, -rcDlg.left, -rcDlg.top);
      InvalidateRect(reinterpret_cast<HWND>(hDlg), &rcLabel, FALSE);
      SendMessageA(hLabel, WM_SETTEXT, 0,
                   reinterpret_cast<LPARAM>(glues_get_str(stru_4ECC40[nPos].wStrId)));
      byte_6673A8 = static_cast<BYTE>(stru_4ECC40[nPos].wSpeed);
      return 0;
    }
    break;
  }
  return SDlgDefDialogProc(reinterpret_cast<void *>(hDlg), nMsg, wParam, lParam);
}

// Initialise the create-game dialog: bitmaps, fonts, every control handle
// this TU keeps, the speed slider, the game list, and the name remembered from
// the last game created.
void sub_44C980(intptr_t hDlg)
{
  HWND hWnd = reinterpret_cast<HWND>(hDlg);

  sub_44D8B0(hDlg);
  memset(xmmword_667388, 0, sizeof(xmmword_667388));
  memset(xmmword_667398, 0, sizeof(xmmword_667398));
  byte_6673A8 = 4;
  sub_44DBE0(hDlg, 0, dword_50341C);
  sub_44DBE0(hDlg, 1, dword_503450);
  dword_667364 = GetDlgItem(hWnd, 0x3E8);
  dword_667368 = GetDlgItem(hWnd, 0x3E9);
  dword_66736C = GetDlgItem(hWnd, 0x402);
  dword_667360 = GetDlgItem(hWnd, 0x3F1);
  dword_66735C = GetDlgItem(hWnd, 0x4B1);
  dword_667374 = GetDlgItem(hWnd, 0x3ED);
  dword_667378 = GetDlgItem(hWnd, 0x409);
  dword_66737C = GetDlgItem(hWnd, 0x40A);
  dword_667380 = GetDlgItem(hWnd, 0x41A);
  dword_667370 = GetDlgItem(hWnd, 0x419);
  dword_667384 = GetDlgItem(hWnd, 0x4B7);
  ShowWindow(static_cast<HWND>(dword_667384), SW_HIDE);
  SendMessageA(static_cast<HWND>(dword_667364), EM_LIMITTEXT, 0x17, 0);
  SendMessageA(static_cast<HWND>(dword_667368), EM_LIMITTEXT, 0x17, 0);
  // The oracle loads the count as a full dword here and as a word below.
  SendMessageA(static_cast<HWND>(dword_667378), 0x406, FALSE,
               MAKELPARAM(0, *reinterpret_cast<int *>(&word_5033FC) - 1));
  SendMessageA(static_cast<HWND>(dword_667378), 0x415, 0, 1);
  SendMessageA(static_cast<HWND>(dword_667378), 0x405, TRUE, (word_5033FC - 1) / 2);
  sub_44D750(reinterpret_cast<intptr_t>(dword_667378));
  SendMessageA(hWnd, WM_HSCROLL, MAKEWPARAM(SB_THUMBPOSITION, (word_5033FC - 1) / 2),
               reinterpret_cast<LPARAM>(dword_667378));
  SendMessageA(static_cast<HWND>(dword_667360), WM_SETREDRAW, FALSE, 0);
  sub_44D320(hDlg, reinterpret_cast<intptr_t>(dword_667360));
  SendMessageA(static_cast<HWND>(dword_667360), WM_SETREDRAW, TRUE, 0);
  sub_44D480(reinterpret_cast<intptr_t>(dword_66735C));

  HWND hName = static_cast<HWND>(dword_667364);
  char szName[0x18] = {0};
  SRegLoadString("Starcraft", "Last Created Game Name", 0, szName, sizeof(szName));
  if (szName[0])
  {
    SendMessageA(hName, WM_SETTEXT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(szName));
    SendMessageA(hName, EM_SETSEL, 0, -1);
  }
}






// The create dialog's OK handler: validate the name, the map and the game type,
// create the game, and report a failure against the control that caused it.
void sub_44CBA0(intptr_t hDlg)
{
  char szDir[0x104];
  char szMap[0x20];
  char szName[0x18];
  char szPassword[0x18];

  sub_49E740(0xA7);
  if (SendMessageA(static_cast<HWND>(dword_667364), WM_GETTEXTLENGTH, 0, 0))
  {
    SendMessageA(static_cast<HWND>(dword_667364), WM_GETTEXT, sizeof(szName),
                 reinterpret_cast<LPARAM>(szName));
    szPassword[0] = 0;
    if (SendMessageA(static_cast<HWND>(dword_667368), WM_GETTEXTLENGTH, 0, 0))
      SendMessageA(static_cast<HWND>(dword_667368), WM_GETTEXT, sizeof(szPassword),
                   reinterpret_cast<LPARAM>(szPassword));
    SendMessageA(static_cast<HWND>(dword_667360), WM_GETTEXT, sizeof(szMap),
                 reinterpret_cast<LPARAM>(szMap));
    if (strcmp(szMap, byte_66732C) == 0)
    {
      // The barrier keeps the zero strcmp result out of this block's first push.
      _ReadWriteBarrier();
nomap:
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x91), "", 0);
      SetFocus(static_cast<HWND>(dword_667360));
      return;
    }
    const TGAMEPARAMS *pParams =
        gametype_find_template(static_cast<BYTE>(dword_667350), static_cast<BYTE>(dword_667354),
                               static_cast<WORD>(dword_667358));
    if (dword_6674B4 == 0 && pParams->bFlag12 == 1)
    {
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x93), "", 0);
      SetFocus(static_cast<HWND>(dword_66735C));
      return;
    }
    if (IsCompetitiveGame(pParams->bCategory, pParams->bSub) && dword_6675BC == 0)
    {
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xC0), "", 0);
      SetFocus(static_cast<HWND>(dword_66735C));
      return;
    }
    if (!SendMessageA(static_cast<HWND>(dword_667360), CB_GETCOUNT, 0, 0))
      goto nomap;
    int nIndex = (int)SendMessageA(static_cast<HWND>(dword_667360), CB_GETCURSEL, 0, 0);
    if (nIndex == -1)
      goto nomap;
    intptr_t nItem = SendMessageA(static_cast<HWND>(dword_667360), CB_GETITEMDATA, nIndex, 0);
    DWORD nPacked = (((static_cast<WORD>(dword_667358) << 8) | static_cast<BYTE>(dword_667354))
                     << 8) |
                    static_cast<BYTE>(dword_667350);
    int nResult = CreateNewGame(nItem, reinterpret_cast<intptr_t>(szName), reinterpret_cast<intptr_t>(szPassword),
                             nPacked, byte_6673A8, szDir);
    if (nResult == 0)
    {
      SDlgEndDialog(reinterpret_cast<void *>(hDlg), 1);
      return;
    }
    // Each arm is written out whole; MSVC folds the shared tails.
    switch (nResult)
    {
    case static_cast<int>(0x80000009):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x92), "", 0);
      if (dword_667368)
        SetFocus(static_cast<HWND>(dword_667368));
      return;
    case static_cast<int>(0x8000000A):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xB8), "", 0);
      if (dword_667368)
        SetFocus(static_cast<HWND>(dword_667368));
      return;
    case static_cast<int>(0x80000006):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x34), "", 0);
      return;
    case static_cast<int>(0x80000002):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x3B), "", 0);
      if (dword_66735C)
        SetFocus(static_cast<HWND>(dword_66735C));
      return;
    case static_cast<int>(0x80000001):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x3C), "", 0);
      if (dword_667360)
        SetFocus(static_cast<HWND>(dword_667360));
      return;
    case static_cast<int>(0x80000004):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x8F), "", 0);
      if (dword_667364)
        SetFocus(static_cast<HWND>(dword_667364));
      return;
    case static_cast<int>(0x80000008):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x36), "", 0);
      if (dword_66735C)
        SetFocus(static_cast<HWND>(dword_66735C));
      return;
    case static_cast<int>(0x80000007):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x33), "", 0);
      if (dword_667360)
        SetFocus(static_cast<HWND>(dword_667360));
      return;
    case static_cast<int>(0x80000003):
    case static_cast<int>(0x80000012):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x32), "", 0);
      if (dword_667360)
        SetFocus(static_cast<HWND>(dword_667360));
      return;
    case static_cast<int>(0x8000000E):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xC1), "", 0);
      if (dword_667360)
        SetFocus(static_cast<HWND>(dword_667360));
      return;
    case static_cast<int>(0x8000000F):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xAB), "", 0);
      if (dword_66735C)
        SetFocus(static_cast<HWND>(dword_66735C));
      return;
    case static_cast<int>(0x80000010):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xBB), "", 0);
      if (dword_667360)
        SetFocus(static_cast<HWND>(dword_667360));
      return;
    case static_cast<int>(0x80000011):
      UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0xC5), "", 0);
      if (dword_667374)
        SetFocus(static_cast<HWND>(dword_667374));
      return;
    }
  }
  else
  {
    UiMessageBoxCallback(reinterpret_cast<void *>(hDlg), glues_get_str(0x90), "", 0);
    SetFocus(static_cast<HWND>(dword_667364));
  }
}

// Release the map preview and the two owned bitmaps.
void sub_44CF10(void)
{
  if (dword_667328 != 0) {
    SMemFree(dword_667328, "gluBNCreate.cpp", 0x593, 0);
    dword_667328 = 0;
  }
  for (int i = 0; i < 2; ++i) {
    if (dword_667AD4[i] != 0) {
      SMemFree(dword_667AD4[i], "gluBNCreate.cpp", 0x598, 0);
      dword_667AD4[i] = 0;
    }
  }
}

// Release the four dialog art buffers.
void sub_44CF70(void)
{
  if (dword_667318 != 0) {
    SMemFree(dword_667318, "gluBNCreate.cpp", 0xF0, 0);
    dword_667318 = 0;
  }
  if (dword_66731C != 0) {
    SMemFree(dword_66731C, "gluBNCreate.cpp", 0xF3, 0);
    dword_66731C = 0;
  }
  if (dword_667320 != 0) {
    SMemFree(dword_667320, "gluBNCreate.cpp", 0xF6, 0);
    dword_667320 = 0;
  }
  if (dword_667324 != 0) {
    SMemFree(dword_667324, "gluBNCreate.cpp", 0xFA, 0);
    dword_667324 = 0;
  }
}





// Owner-draw one browse-list row: the folder or map icon, then the text in
// white on the highlight colour when the row is selected and focused.
void sub_44D010(intptr_t nUnused, void *pArg1)
{
  DRAWITEMSTRUCT *pDis = static_cast<DRAWITEMSTRUCT *>(pArg1);
  BYTE mapInfo[0xB4];
  int bSelected;

  (void)nUnused;
  if (pDis->hDC == NULL)
    return;
  // The shared zero store is one block; a structured if/else splits it.
  if (pDis->itemState & ODS_SELECTED)
  {
    HWND hFocus = GetFocus();
    bSelected = 1;
    if (hFocus == pDis->hwndItem)
      goto have_selection;
  }
  bSelected = 0;
have_selection:
  if (!CreateMapInfo(reinterpret_cast<void *>(pDis->itemData), mapInfo))
    return;

  int nIcon = mapInfo[4] != 8;
  if (dword_667AD4[nIcon] != NULL)
  {
    blt_rect_t rc;
    rc.nLeft = 0;
    rc.nTop = 0;
    rc.nRight = stru_667ADC[nIcon].cx;
    rc.nBottom = stru_667ADC[nIcon].cy;
    SDlgBltToWindowE(reinterpret_cast<DWORD_PTR>(pDis->hwndItem), pDis->rcItem.left,
                     (pDis->rcItem.bottom - pDis->rcItem.top) / 2 - stru_667ADC[nIcon].cy / 2 +
                         pDis->rcItem.top,
                     reinterpret_cast<DWORD_PTR>(dword_667AD4[nIcon]), &rc, &stru_667ADC[nIcon],
                     0xFFFFFFFF);
    pDis->rcItem.left += stru_667ADC[nIcon].cx + 2;
  }

  COLORREF crText = SetTextColor(pDis->hDC, 0xFFFFFF);
  COLORREF crBack = SetBkColor(pDis->hDC, bSelected ? GetSysColor(COLOR_HIGHLIGHT) : 0);
  int cchText = (int)SendMessageA(pDis->hwndItem, LB_GETTEXTLEN, pDis->itemID, 0);
  char *pszText = static_cast<char *>(SMemAlloc(cchText + 1, "gluBNCreate.cpp", 0x64E, 0));
  SendMessageA(pDis->hwndItem, LB_GETTEXT, pDis->itemID, reinterpret_cast<LPARAM>(pszText));
  TextOutA(pDis->hDC, pDis->rcItem.left, pDis->rcItem.top, pszText, cchText);
  SMemFree(pszText, "gluBNCreate.cpp", 0x651, 0);
  SetTextColor(pDis->hDC, crText);
  SetBkColor(pDis->hDC, crBack);
}






// Owner-draw the browse dialog's title: the map's name centred in the item,
// led by the icon of whichever of four map-info flags is set.
void sub_44D190(intptr_t nUnused, void *pArg1)
{
  DRAWITEMSTRUCT *pDis = static_cast<DRAWITEMSTRUCT *>(pArg1);
  BYTE mapInfo[0xB4];
  SIZE size;
  int nIcon;

  (void)nUnused;
  if (pDis->hDC == NULL)
    return;
  if (!CreateMapInfo(dword_667AD0, mapInfo))
    return;

  COLORREF crText = SetTextColor(pDis->hDC, 0xFFFFFF);
  COLORREF crBack = SetBkColor(pDis->hDC, 0);
  int cchBuf = (int)SendMessageA(pDis->hwndItem, WM_GETTEXTLENGTH, 0, 0) + 1;
  char *pszText = static_cast<char *>(SMemAlloc(cchBuf, "gluBNCreate.cpp", 0x664, 0));
  SendMessageA(pDis->hwndItem, WM_GETTEXT, cchBuf, reinterpret_cast<LPARAM>(pszText));
  GetTextExtentPoint32A(pDis->hDC, pszText, cchBuf - 1, &size);
  if (*reinterpret_cast<DWORD *>(&mapInfo[0x18]))
    nIcon = 0x1A;
  else if (*reinterpret_cast<DWORD *>(&mapInfo[0x1C]))
    nIcon = 0x45;
  else if (*reinterpret_cast<DWORD *>(&mapInfo[0x14]))
    nIcon = 0x19;
  else if (*reinterpret_cast<DWORD *>(&mapInfo[0x20]))
    nIcon = 0x47;
  else
    goto draw_text;
  {
    int nYOfs = (pDis->rcItem.bottom - pDis->rcItem.top) / 4 - bn_icons_get_size(nIcon)->cy / 2;
    blt_size_t *pSize = bn_icons_get_size(nIcon);
    // nX is named before nL so that nL, named later, is the base of x's lea.
    int nX = 0;
    int nL = pDis->rcItem.left;
    int nW = (pDis->rcItem.right - nL + 1) / 2 - size.cx / 2;
    nL -= 3;
    nX = nW - pSize->cx;
    // The volatile read keeps y after x; a plain read is merged with nYOfs's (389 B).
    sub_44DFB0(pDis->hwndItem, nL + nX, *(volatile LONG *)&pDis->rcItem.top + nYOfs, nIcon);
  }
draw_text:
  DrawTextA(pDis->hDC, pszText, -1, &pDis->rcItem, DT_CENTER);
  SMemFree(pszText, "gluBNCreate.cpp", 0x68A, 0);
  SetTextColor(pDis->hDC, crText);
  SetBkColor(pDis->hDC, crBack);
}

// Wire the browse dialog's eight map-setting labels, then fill the game-type
// combo and select the entry the directory walk reports.
void sub_44D320(intptr_t hDlg, intptr_t hList)
{
  HWND const hWnd = reinterpret_cast<HWND>(hDlg);

  CreateSetControl(0, GetDlgItem(hWnd, 0x3F7), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(1, GetDlgItem(hWnd, 0x3F3), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(4, GetDlgItem(hWnd, 0x3F5), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(7, GetDlgItem(hWnd, 0x3F5), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(5, GetDlgItem(hWnd, 0x3F6), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(6, GetDlgItem(hWnd, 0x3F9), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(9, GetDlgItem(hWnd, 0x406), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));
  CreateSetControl(8, GetDlgItem(hWnd, 0x42C), reinterpret_cast<void *>(sub_44B6F0),
                   reinterpret_cast<void *>(sub_44B760));

  int bAtRoot = SStrCmpI(sgBNMapsDir_g, sgBNCurrentDir_g, 0x7FFFFFFF) == 0;
  int nSel = FillMapList(0x10, sgBNCurrentDir_g, bAtRoot,
                        reinterpret_cast<PFN_LBOXADD>(sub_44B5A0), 0);
  if (nSel != 0xFF)
  {
    SendMessageA(reinterpret_cast<HWND>(hList), 0x14E, nSel, 0);
    SendMessageA(hWnd, 0x111, 0x103F1, hList);
  }
}

// Refill the game-type combo, then restore whatever the registry remembers and
// tell the parent the selection changed.
void sub_44D480(intptr_t hWnd)
{
  char szSaved[0x20];
  HWND const h = reinterpret_cast<HWND>(hWnd);

  SendMessageA(h, 0x141, 0x1F, 0);
  SendMessageA(h, 0x14B, 0, 0);
  SendMessageA(h, 0x0B, 0, 0);
  IterateMainTemplates(reinterpret_cast<TEMPLATEPROC>(sub_44B690));
  SendMessageA(h, 0x14E, 0, 0);
  SRegLoadString("Starcraft", "Game Type", 0, szSaved, sizeof(szSaved));
  if (szSaved[0] != 0)
    SendMessageA(h, 0x14D, -1, reinterpret_cast<LPARAM>(szSaved));
  SendMessageA(GetParent(h), 0x111, 0x104B1, reinterpret_cast<LPARAM>(h));
  SendMessageA(h, 0x0B, 1, 0);
}

// Rebuild the map list for the current directory and hand the focus back.
void sub_44D520(intptr_t hParent, intptr_t hList)
{
  HWND const h = reinterpret_cast<HWND>(hList);

  EnableWindow(h, 0);
  SendMessageA(h, 0x0B, 0, 0);
  SendMessageA(h, 0x184, 0, 0);

  int bAtRoot = SStrCmpI(sgBNMapsDir_g, sgBNCurrentDir_g, 0x7FFFFFFF) == 0;
  BYTE nSel = static_cast<BYTE>(
      FillMapList(0x28, sgBNCurrentDir_g, bAtRoot,
                 reinterpret_cast<PFN_LBOXADD>(sub_44B560),
                 byte_6674B8[0] == 0 ? 0 : byte_6674B8));

  if (nSel != 0xFF) {
    SendMessageA(h, 0x186, nSel, 0);
    SendMessageA(reinterpret_cast<HWND>(hParent), 0x111, 0x104B0,
                 reinterpret_cast<LPARAM>(h));
  }
  SendMessageA(h, 0x0B, 1, 0);
  EnableWindow(h, 1);
  SetFocus(h);
}

// Refill the subtype combo for a newly selected map, unless the list was built
// for that map already.
void sub_44D5D0(int hWnd)
{
  if (dword_667350 != 0x0F)
    return;
  if (dword_6676C4 != -1 && hWnd == dword_6676C4)
    return;

  HWND hFocus = GetFocus();

  dword_6676C4 = hWnd;
  sub_44D620(reinterpret_cast<intptr_t>(dword_667374), (int)dword_667350, (int)dword_667354);
  SetFocus(hFocus);
}

// Refill the subtype combo from one category's league templates, then restore
// the remembered selection and tell the parent.
void sub_44D620(intptr_t hWnd, int nCategory, int nSub)
{
  char szSaved[0x20];
  HWND const h = reinterpret_cast<HWND>(hWnd);

  SendMessageA(h, 0x0B, 0, 0);
  SendMessageA(h, 0x14B, 0, 0);
  SendMessageA(h, 0x141, 0x1F, 0);
  IterateLeagueTemplates(static_cast<BYTE>(nCategory), static_cast<BYTE>(nSub),
                         reinterpret_cast<TEMPLATEPROC>(sub_44B610));

  LONG nCount = (LONG)SendMessageA(h, 0x146, 0, 0);
  if (nCount > 0) {
    if (nCategory == 0x0F)
      SendMessageA(h, 0x14E, nCount / 2, 0);
    else
      SendMessageA(h, 0x14E, 0, 0);
  }
  SRegLoadString("Starcraft", "Game Subtype", 0, szSaved, sizeof(szSaved));
  if (szSaved[0] != 0)
    SendMessageA(h, 0x14D, -1, reinterpret_cast<LPARAM>(szSaved));
  SendMessageA(GetParent(h), 0x111, 0x103ED, reinterpret_cast<LPARAM>(h));
  SendMessageA(h, 0x0B, 1, 0);
}

// One line's height in hFont, measured on a scratch DC over the desktop.
int sub_44D6E0(intptr_t hFont)
{
  HDC hdc = GetDC(GetDesktopWindow());
  int nHeight;
  TEXTMETRIC_A tm;
  HDC hdcMem = CreateCompatibleDC(hdc);
  HGDIOBJ hOld = SelectObject(hdcMem, reinterpret_cast<HGDIOBJ>(hFont));

  nHeight = -1;
  if (GetTextMetricsA(hdcMem, &tm))
    nHeight = tm.tmHeight;
  SelectObject(hdcMem, hOld);
  DeleteDC(hdcMem);
  ReleaseDC(GetDesktopWindow(), hdc);
  return nHeight;
}

// Move the speed slider to whatever the registry remembers, matching on the
// localised label rather than on the stored index.
void sub_44D750(intptr_t hDlg)
{
  char szSaved[0x80];

  SRegLoadString("Starcraft", "Game Speed", 0, szSaved, sizeof(szSaved));
  for (int i = 0; i < word_5033FC; ++i) {
    if (_stricmp(glues_get_str(stru_4ECC40[i].wStrId), szSaved) == 0) {
      SendMessageA(reinterpret_cast<HWND>(hDlg), 0x405, 1, i);
      return;
    }
  }
}

int sub_44D7D0(void)
{
  if (dword_667334 != 0 && dword_667330 == 0)
    return 1;
  return 0;
}

// Load the browse dialog's background and its two icon bitmaps, dropping any
// icon whose file is missing.
void sub_44D7F0(intptr_t hDlg)
{
  int ids[3] = { 1, 2, 0 };

  LoadArtFile(reinterpret_cast<void *>(hDlg), GetParent(reinterpret_cast<HWND>(hDlg)),
              "Browse", -1, 1, "glue\\battle.net\\backgrounds\\bnfile.pcx",
              &dword_667328, 0, 1);
  for (int i = 0; i < 2; ++i) {
    if (!LoadArtFile(0, 0, "Icon", -1, 1, off_5033F4[i], &dword_667AD4[i],
                     &stru_667ADC[i], 0)) {
      dword_667AD4[i] = 0;
      memset(&stru_667ADC[i], 0, sizeof(stru_667ADC[i]));
    }
  }
  SDlgSetControlBitmaps(hDlg, ids, 0, dword_66731C, &stru_667338, 1, -1);
}

// Load the create-game dialog's background and its three button bitmaps.
void sub_44D8B0(intptr_t hDlg)
{
  int idsBg[3] = { 1, 2, 0 };
  int idsSmall[2] = { 0x3F0, 0 };
  int idsWide[2] = { 0x4B7, 0 };

  LoadArtFile(reinterpret_cast<void *>(hDlg), GetParent(reinterpret_cast<HWND>(hDlg)),
              "Create", -1, 1, "glue\\battle.net\\backgrounds\\creat_bg.pcx",
              &dword_667318, 0, 1);
  LoadArtFile(0, 0, "Button", -1, 1,
              "glue\\battle.net\\generic\\icons\\but_xsm.pcx",
              &dword_66731C, &stru_667338, 1);
  LoadArtFile(0, 0, "Button", -1, 1,
              "glue\\battle.net\\generic\\icons\\but_110x35.pcx",
              &dword_667320, &stru_667340, 1);
  LoadArtFile(0, 0, "Button", -1, 1,
              "glue\\battle.net\\generic\\icons\\but_med.pcx",
              &dword_667324, &stru_667348, 1);
  SDlgSetControlBitmaps(hDlg, idsBg, 0, dword_66731C, &stru_667338, 1, -1);
  SDlgSetControlBitmaps(hDlg, idsSmall, 0, dword_667320, &stru_667340, 1, -1);
  SDlgSetControlBitmaps(hDlg, idsWide, 0, dword_667324, &stru_667348, 1, -1);
}

// Open a file through its shell association, falling back to whatever
// FindExecutableA turns up for the scratch file's extension.
//
// The second probe sets the flag and then clears it on failure; that is what
// gives the oracle's `sbb esi, esi / and esi, 1h` where a boolean gives `neg`.
int sub_44D9D0(const char *pszFile)
{
  char szExe[0x104];

  if (reinterpret_cast<uintptr_t>(
          ShellExecuteA(static_cast<HWND>(SDrawGetFrameWindow(0)), "open",
                        pszFile, 0, 0, 1)) <= 0x20) {
    int bOpened;

    fclose(fopen(off_503418, "wb"));
    if (reinterpret_cast<uintptr_t>(FindExecutableA(off_503418, 0, szExe)) <= 0x20) {
      bOpened = 0;
    } else {
      bOpened = 1;
      if (reinterpret_cast<uintptr_t>(
              ShellExecuteA(static_cast<HWND>(SDrawGetFrameWindow(0)), "open",
                            szExe, pszFile, 0, 1)) <= 0x20)
        bOpened = 0;
    }
    DeleteFileA(off_503418);
    return bOpened;
  }
  return 1;
}

// Save the focused combo box's text under the given registry value name. The
// three bodies differ only in the buffer size and the value name.
void sub_44DA90(intptr_t hCtrl)
{
  char szText[0x18];

  if (SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x147, 0, 0) != -1) {
    SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x0D, sizeof(szText),
                 reinterpret_cast<LPARAM>(szText));
    SRegSaveString("Starcraft", "Last Created Game Name", 0, szText);
  }
}

// Persist the speed slider's current label rather than its index.
void sub_44DAE0(intptr_t hCtrl)
{
  char szText[0x80];

  strcpy(szText, glues_get_str(
                     stru_4ECC40[SendMessageA(reinterpret_cast<HWND>(hCtrl),
                                              0x400, 0, 0)].wStrId));
  SRegSaveString("Starcraft", "Game Speed", 0, szText);
}

void sub_44DB40(intptr_t hCtrl)
{
  char szText[0x20];

  if (SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x147, 0, 0) != -1) {
    SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x0D, sizeof(szText),
                 reinterpret_cast<LPARAM>(szText));
    SRegSaveString("Starcraft", "Game Subtype", 0, szText);
  }
}

void sub_44DB90(intptr_t hCtrl)
{
  char szText[0x20];

  if (SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x147, 0, 0) != -1) {
    SendMessageA(reinterpret_cast<HWND>(hCtrl), 0x0D, sizeof(szText),
                 reinterpret_cast<LPARAM>(szText));
    SRegSaveString("Starcraft", "Game Type", 0, szText);
  }
}

// Rebuild the dialog's font at one of the table's point sizes and hand it to
// every control the caller's zero-terminated id list names.
intptr_t sub_44DBE0(intptr_t hDlg, int nFontIndex, const int *pIds)
{
  LOGFONT_A lf;
  HWND const h = reinterpret_cast<HWND>(hDlg);
  HGDIOBJ hFont = reinterpret_cast<HGDIOBJ>(SendMessageA(h, 0x31, 0, 0));

  if (hFont == 0)
    return 0;
  memset(&lf, 0, sizeof(lf));
  if (!GetObjectA(hFont, sizeof(lf), &lf))
    return 0;
  lf.lfHeight = -MulDiv(stru_503400[nFontIndex].nPoints, 0x60, 0x48);
  lf.lfWeight = 0x190;
  strcpy(lf.lfFaceName, stru_503400[nFontIndex].pszFace);

  HGDIOBJ hNew = CreateFontIndirectA(&lf);
  if (hNew == 0)
    return 0;
  for (int i = 0; pIds[i] != 0; ++i)
    SendDlgItemMessageA(h, pIds[i], 0x30, reinterpret_cast<WPARAM>(hNew), 0);
  return reinterpret_cast<intptr_t>(hNew);
}

// Set a static's text and invalidate the rectangle it occupied, expressed in
// the parent's client coordinates.
void sub_44DCB0(intptr_t hWnd, intptr_t pszText)
{
  RECT rcParent;
  RECT rcSelf;
  HWND const hSelf = reinterpret_cast<HWND>(hWnd);

  GetWindowRect(GetParent(hSelf), &rcParent);
  GetWindowRect(hSelf, &rcSelf);
  OffsetRect(&rcSelf, -rcParent.left, -rcParent.top);
  InvalidateRect(GetParent(hSelf), &rcSelf, 0);
  SendMessageA(hSelf, 0x0C, 0, pszText);
}

// Show or hide the eight game-option controls for the map now selected.
void sub_44DD20(void)
{
  BYTE mapInfo[0xB4];

  if (!CreateMapInfo(dword_667AD0, mapInfo))
    return;
  if (mapInfo[4] & 0x80) {
    ShowWindow(static_cast<HWND>(dword_667378), 0);
    ShowWindow(static_cast<HWND>(dword_66737C), 0);
    ShowWindow(static_cast<HWND>(dword_667380), 0);
    ShowWindow(static_cast<HWND>(dword_66735C), 0);
    ShowWindow(static_cast<HWND>(dword_667370), 0);
    ShowWindow(static_cast<HWND>(dword_667374), 0);
    ShowWindow(static_cast<HWND>(dword_66736C), 0);
    ShowWindow(static_cast<HWND>(dword_667384), 0);
  } else {
    ShowWindow(static_cast<HWND>(dword_667378), 1);
    ShowWindow(static_cast<HWND>(dword_66737C), 1);
    ShowWindow(static_cast<HWND>(dword_667380), 1);
    ShowWindow(static_cast<HWND>(dword_66735C), 1);
    ShowWindow(static_cast<HWND>(dword_667370), 1);
    ShowWindow(static_cast<HWND>(dword_667374), byte_6679D0[0] != 0);
    ShowWindow(static_cast<HWND>(dword_66736C), byte_6679D0[0] != 0);
    ShowWindow(static_cast<HWND>(dword_667384), IsWGTGame());
  }
}

// Adopt the map the browse combo now selects.
void sub_44DE20(intptr_t hDlg)
{
  HWND const hWnd = reinterpret_cast<HWND>(hDlg);

  LONG nSel = (LONG)SendDlgItemMessageA(hWnd, 0x3F1, 0x147, 0, 0);

  if (nSel != -1) {
    void *pEntry = reinterpret_cast<void *>(
        SendDlgItemMessageA(hWnd, 0x3F1, 0x150, nSel, 0));

    sub_45B970(pEntry);
    dword_667AD0 = pEntry;
  }
}

int sub_44DE70(void)
{
  return dword_667354;
}

intptr_t sub_44DE80(void)
{
  return dword_667358;
}

intptr_t sub_44DE90(void)
{
  return dword_667350;
}

char *sub_44DEA0(void)
{
  return byte_6677D0;
}
