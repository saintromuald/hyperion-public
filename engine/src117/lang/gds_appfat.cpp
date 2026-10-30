// gds_appfat.cpp -- the fatal-error dialogs: the dialog procedure and the
// wrappers that show each template. Bodies are in oracle VA order.

#include "../h/gds_appfat.h"
#include "../h/gds_local.h"
#include "../h/gds_appcrit.h"
#include "../h/gds_sys_Win.h"

#include <string.h>  // strrchr, as a const char * overload

#include "../gen/globals117.h"
#include "../port/tools/port_crash.h"  // Hyperion: PortCrashFatal

// oracle 0x4CEA00 (152 B). The dialog procedure every wrapper in this TU hands
// to GdsDialogBoxParam. WM_COMMAND is written first because source case order
// sets the physical block order, and the two arms share their tail: MSVC merges
// them down to the pushed EndDialog result.
INT_PTR CALLBACK ErrorDlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
  case WM_COMMAND:
    if (1 == static_cast<WORD>(wParam))
    {
      EndDialog(hDlg, 1);
      ShowCursor(0);
      SetCursor(NULL);
    }
    else if (2 == static_cast<WORD>(wParam))
    {
      EndDialog(hDlg, 0);
      ShowCursor(0);
      SetCursor(NULL);
    }
    return 1;
  case WM_INITDIALOG:
    SetCursor(LoadCursorA(NULL, IDC_ARROW));
    ShowCursor(1);
    center_window(hDlg);
    if (lParam != 0)
      SetDlgItemTextA(hDlg, 1000, reinterpret_cast<const char *>(lParam));
    return 1;
  }
  return 0;
}

// oracle 0x4CEAA0 (24 B), template id 0x6C.
BOOL ProcErrorDlg()
{
  return GdsDialogBoxParam(0x6C, ErrorDlgProc, NULL) == 1;
}

// oracle 0x4CEAC0 (55 B), template id 0x70. ECX is the megabyte count the
// "%d M" at 0x4F1DFC formats.
BOOL DiskFreeErrorDlg(int nMegs)
{
  char szText[0x40];

  psnprintf(szText, sizeof(szText), "%d M", nMegs);
  return GdsDialogBoxParam(0x70, ErrorDlgProc, szText) == 1;
}

// oracle 0x4CEB00 (137 B), template id from ECX. Strips the path off the file
// name, formats "%s\n%s line %d\n%s" at 0x4F1E0C, and never comes back:
// app_fatal ends in ExitProcess.
void ErrorDlg(int nTemplate, DWORD dwError, const char *pszFile,
                         int nLineNo, const char *pszText)
{
  char szText[0x200];
  char szError[0x100];
  const char *pszBase = strrchr(pszFile, '\\');

  if (pszBase != NULL)
    pszFile = pszBase + 1;
  psnprintf(szText, sizeof(szText), "%s\n%s line %d\n%s",
            strGetError(dwError, szError, sizeof(szError)), pszFile, nLineNo,
            pszText != NULL ? pszText : "");
  PortCrashFatal(szText);  // Hyperion: the dialog can be out of sight
  app_cleanup();
  GdsDialogBoxParam(nTemplate, ErrorDlgProc, szText);
  app_fatal();
}

// oracle 0x4CEB90 (96 B), template id 0x6A. Formats "%s\n%s" at 0x4F1E04 and
// ends the same way ErrorDlg does.
void FileErrorDlg(const char *pszFile, DWORD dwError)
{
  char szText[0x200];
  char szError[0x100];

  psnprintf(szText, sizeof(szText), "%s\n%s", pszFile,
            strGetError(dwError, szError, sizeof(szError)));
  PortCrashFatal(szText);  // Hyperion: the dialog can be out of sight
  app_cleanup();
  GdsDialogBoxParam(0x6A, ErrorDlgProc, szText);
  app_fatal();
}

// oracle 0x4CEBF0 (24 B), template id 0x6B.
BOOL InsertCDDlg()
{
  return GdsDialogBoxParam(0x6B, ErrorDlgProc, NULL) == 1;
}

// oracle 0x4CEC10 (24 B), template id 0x6D.
BOOL MemErrorDlg()
{
  return GdsDialogBoxParam(0x6D, ErrorDlgProc, NULL) == 1;
}

// oracle 0x4CEC30 (24 B), template id 0x6F.
BOOL VRamErrorDlg()
{
  return GdsDialogBoxParam(0x6F, ErrorDlgProc, NULL) == 1;
}
