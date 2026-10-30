// gds_appfat.h -- declarations for src117/lang/gds_appfat.cpp, the fatal-error
// dialogs. Free functions are __fastcall through /Gr and are declared without
// the keyword.

#ifndef OC117_GDS_APPFAT_H
#define OC117_GDS_APPFAT_H

#include "platform.h"
#include <windows.h>   // the dialog proc signature

// 0x4CEA00 (152 B). The Win32 dialog callback, stdcall with `ret 10h`; the
// wrappers below hand its address to GdsDialogBoxParam.
INT_PTR CALLBACK ErrorDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Each wrapper is the same body with a different template id, and each returns
// the `== 1` of the dialog result rather than the result itself.
BOOL ProcErrorDlg();                // 0x4CEAA0 (24 B), id 0x6C
BOOL DiskFreeErrorDlg(int nMegs);   // 0x4CEAC0 (55 B), id 0x70
BOOL InsertCDDlg();                 // 0x4CEBF0 (24 B), id 0x6B
BOOL MemErrorDlg();                 // 0x4CEC10 (24 B), id 0x6D
BOOL VRamErrorDlg();                // 0x4CEC30 (24 B), id 0x6F

// Both return void and never come back: app_fatal ends in ExitProcess. All 35
// of their call sites discard EAX.
void ErrorDlg(int nTemplate, DWORD dwError, const char *pszFile,
              int nLineNo, const char *pszText);          // 0x4CEB00 (137 B)
void FileErrorDlg(const char *pszFile, DWORD dwError);    // 0x4CEB90 (96 B), id 0x6A

#endif  // OC117_GDS_APPFAT_H
