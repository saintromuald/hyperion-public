// gluBNCreate.h -- declarations for src117/lang/gluBNCreate.cpp: battle.net glue
//
// Class members belong in the class header; this file owns the free functions. They are
// spelled without an explicit __fastcall, which /Gr already makes the default.

#ifndef OC117_GLUBNCREATE_H
#define OC117_GLUBNCREATE_H

#include "platform.h"

// Callees this TU reaches whose home TU is elsewhere, declared here so this TU compiles:
//   IsSimpleTemplate          0x447DE0  gametype.cpp
//   gametype_get_subtype_name 0x447BB0  gametype.cpp
//   CreateMapInfo             0x45BEB0  gluBNCreate.cpp
//   sub_45B970                0x45B970  gluBNCreate.cpp
struct TGAMEPARAMS;

// The browse list's per-entry add callback, the shape sub_44B560 has.
typedef int(__stdcall *PFN_LBOXADD)(intptr_t nItemData, intptr_t nText, int nUnused);

int IsSimpleTemplate(BYTE bCategory, BYTE bSub, WORD wType);
const TGAMEPARAMS *gametype_find_template(BYTE bCategory, BYTE bSub, WORD wType);
int FillMapList(BYTE bKind, const char *pszDir, int bAtRoot, PFN_LBOXADD pfnAdd,
               const char *pszFilter);
int CreateNewGame(intptr_t nItem, intptr_t nFlags, intptr_t nArg0, int nArg1, int nArg2, char *pszDir);
int sub_44BDC0(intptr_t hDlg, const char *pszDir);
// The message is unsigned: the body's range checks are `ja`.
intptr_t __stdcall ChatCreateGameDialogProc(intptr_t hDlg, unsigned nMsg, intptr_t wParam, intptr_t lParam);
void sub_45C680(char *pszDir);
void gametype_get_subtype_name(char *pszDest, int cchDest,
                               const TGAMEPARAMS *pParams, int nMax);
int CreateMapInfo(void *pMapEntry, void *pInfo);
// 0x45C6A0 CreateSetControl -- `ret 8h`, ECX + EDX + two stack words; all 24
// call sites discard EAX. ECX is the setting slot, EDX the control or window
// it drives, then the set-text and show/hide callbacks.
void CreateSetControl(int nIndex, void *pData, void *pfnSet, void *pfnEnable);
void sub_45B970(void *pMapEntry);

// The three list-fill callbacks the browse dialog hands to the directory and
// template walkers. Each is reached only through a function pointer, so the arity off
// `ret 0Ch` is a lower bound; the pointer is taken at 0x44D520 (sub_44B560), 0x44D480
// and 0x44D620 (the pair below).
int __stdcall sub_44B560(int nItemData, int nText, int nUnused);
int __stdcall sub_44B5A0(int nItemData, int nText, int nFlags);
void __stdcall sub_44B760(int nUnused, intptr_t hWnd, int bShow);

// Free the browse dialog's owned art. Nullary, EAX discarded by the one caller.
void sub_44CF10(void);
void sub_44CF70(void);

// Height of one line in hFont, or -1 when GetTextMetricsA fails. ECX in, the
// one caller uses the full 32 bits of EAX.
int sub_44D6E0(intptr_t hFont);

// True while a map is selected and no directory walk is pending. Nullary.
int sub_44D7D0(void);

// Persist one combo box's current text under HKCU\...\Starcraft. ECX in.
void sub_44DA90(intptr_t hCtrl);
void sub_44DB40(intptr_t hCtrl);
void sub_44DB90(intptr_t hCtrl);

// Rebuild the dialog font at one of the table's sizes. ECX and EDX in, one
// stack word; the four callers discard EAX but the body returns the new font.
intptr_t sub_44DBE0(intptr_t hDlg, int nFontIndex, const int *pIds);

// Set a static's text and repaint the rectangle it used to occupy in parent
// coordinates. ECX and EDX in.
void sub_44DCB0(intptr_t hWnd, intptr_t pszText);

// The two template-walk callbacks the browse dialog hands to gametype.cpp's
// Iterate* pair. Both are TEMPLATEPROCs: `ret 10h` and the third argument
// dereferenced as a TGAMEPARAMS.
void __stdcall sub_44B610(int nArg0, int nArg1, intptr_t nArg2, int nArg3);
void __stdcall sub_44B690(int nArg0, int nArg1, intptr_t nArg2, int nArg3);

// Enter the directory the browse list has selected. ECX in; both callers use
// the full 32 bits of EAX.
int sub_44BCB0(intptr_t hDlg);

// The Battle.net create-game entry point, reached through a callback table.
// `ret 18h`, six stack words.
int __stdcall UiCreateGameCallback(int nArg0, int nArg1, intptr_t nInfo, intptr_t nArg3,
                                   int nArg4, intptr_t nArg5);

// Re-enable the player-count slider for one template. ECX and EDX in, two
// stack words; the one caller discards EAX. nSub is int: the body reads only
// its low byte, but sub_44C380 pushes the whole dword unmasked.
void sub_44BF30(intptr_t hDlg, int nCategory, int nSub, int nType);

// Mirror the selected template's name into a static when the selection is
// simple. Address-taken only, alongside sub_44B760.
void __stdcall sub_44B6F0(int nEvent, intptr_t hWnd, intptr_t pszText);

// Open a file through its shell association. ECX in.
int sub_44D9D0(const char *pszFile);

// Rebuild the map list for the current directory. ECX and EDX in.
void sub_44D520(intptr_t hParent, intptr_t hList);

// Wire the eight map-setting labels and fill the game-type combo. ECX and EDX
// in; both callers discard EAX.
void sub_44D320(intptr_t hDlg, intptr_t hList);

// 0x44BA20. ECX the browse dialog; the one caller discards EAX.
void sub_44BA20(intptr_t hDlg);

// 0x44B780. The browse dialog's procedure; `ret 10h`, reached only by address.
intptr_t __stdcall glubn_createbrowse_wndproc(void *hWnd, unsigned uMsg, uintptr_t wParam,
                                         LONG_PTR lParam);

// 0x44C050. ECX the create dialog, EDX the button that opened the browser; the
// one caller discards EAX.
void gluBNCreate_CreateBrowseDlg(void *hDlg, void *hButton);

// 0x44C380. The create dialog's WM_COMMAND handler: ECX the dialog, EDX the
// wParam, then the lParam and a result out-slot (`ret 8h`). Returns nonzero
// when it handled the command.
int sub_44C380(intptr_t hDlg, intptr_t wParam, intptr_t lParam, int *pResult);

// 0x44CBA0. The create dialog's OK handler; ECX the dialog, EAX discarded.
void sub_44CBA0(intptr_t hDlg);

// 0x44C980. ECX the create-game dialog; the one caller discards EAX.
void sub_44C980(intptr_t hDlg);

// The browse list's two owner-draw painters. ECX is never read; EDX is the
// DRAWITEMSTRUCT. The one caller discards EAX.
void sub_44D010(intptr_t nUnused, void *pArg1);
void sub_44D190(intptr_t nUnused, void *pArg1);

// Load this dialog's art. ECX in; the one caller discards EAX each time.
void sub_44D7F0(intptr_t hDlg);
void sub_44D8B0(intptr_t hDlg);

// Refill the game-type combo and restore the remembered selection. ECX in.
void sub_44D480(intptr_t hWnd);

// Refill the subtype combo for one category, preserving the focus. ECX in.
void sub_44D5D0(int hWnd);
void sub_44D620(intptr_t hWnd, int nCategory, int nSub);

// Show or hide the game-option controls for the selected map. Nullary.
void sub_44DD20(void);

// Refill the game-speed combo from the registry. ECX in.
void sub_44D750(intptr_t hDlg);

// Persist the game-speed slider's label. ECX in.
void sub_44DAE0(intptr_t hCtrl);

// Hand the selected map entry to sub_45B970 and remember it. ECX in.
void sub_44DE20(intptr_t hDlg);

// The four accessors over this TU's selection state.
int sub_44DE70(void);
intptr_t sub_44DE80(void);
intptr_t sub_44DE90(void);
char *sub_44DEA0(void);

#endif  // OC117_GLUBNCREATE_H
