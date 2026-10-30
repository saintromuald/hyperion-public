// port_install.cpp -- which StarCraft install the game's files come from, and the first start's
// choice of it (docs/modules/data.md, "Which install, as the player chose it").
//
// Two kinds of install give the game's files: StarCraft from Battle.net, the free StarCraft,
// whose storage Hyperion reads (port_casc.cpp, port_remastered.cpp), and a classic StarCraft
// folder (1.16.1 and earlier, or 1.17) with its MPQs and Local.dll. settings.json keeps the
// kind ("data": "remastered" or "mpq") and each one's folder ("remastered", "classic"), which
// Settings' Hyperion tab and the first start set. The command line names them for one run
// (--install, --install-folder), before HYPERION_DATA_SOURCE, HYPERION_REMASTERED and
// HYPERION_GAME, which come before settings.json. With no kind given or kept, it is found as
// before: 1.17's MPQs in the game's folder, else StarCraft from Battle.net where it is
// installed. Either way the game is drawn in the classic art (port_casc.cpp, the PRD's
// decisions of 2026-10-21).
//
// A start a player made with no base kept, or whose base's files are gone, asks for the folder of
// each install they have, says whether the game's files are in each, and which is the base, the
// one the game reads; the other stays linked, to switch to in Settings. On Windows in a window of
// Windows' own controls (hyperion.rc), elsewhere in one Hyperion draws (port_install_window.cpp).
// A start for a replay, a benchmark, a lab worker, a LAN room's seat or a script's --install says
// what is missing instead.
#include <windows.h>
#include <string>
#include <SDL3/SDL.h>
#include "port_install.h"
#include "../front/port_text.h"
#include "../front/port_ui.h"            // PortCascAvailable, PortCascCheck, PortCascFolder, PortCascUse
#include "../platform/port_paths.h"      // PortCommandOptions, PortProgramFolder
#include "../platform/port_settings.h"
#if defined(_WIN32)
#include <shobjidl.h>  // IFileOpenDialog
#include "port_install_dlg.h"
#endif

PortInstallKind PortInstallChosen(void)
{
  char szKind[32] = "";
  PortSettingString("hyperion", "data", szKind, sizeof szKind);
  if (SDL_strcasecmp(szKind, "remastered") == 0)
    return PORT_INSTALL_BATTLENET;
  if (SDL_strcasecmp(szKind, "mpq") == 0)
    return PORT_INSTALL_CLASSIC;
  return PORT_INSTALL_UNSET;
}

static PortInstallKind CommandLineKind(void)
{
  const PortOptions *o = PortCommandOptions();
  if (SDL_strcasecmp(o->szInstall, "classic") == 0 || SDL_strcasecmp(o->szInstall, "mpq") == 0)
    return PORT_INSTALL_CLASSIC;
  if (SDL_strcasecmp(o->szInstall, "remastered") == 0 || SDL_strcasecmp(o->szInstall, "battlenet") == 0)
    return PORT_INSTALL_BATTLENET;
  if (o->szInstall[0] != 0)
    return PORT_INSTALL_UNSET;  // a word --install does not know, which the start refuses
  if (o->szInstallFolder[0] != 0) {
    char szInstall[MAX_PATH];
    if (PortInstallClassicCheck(o->szInstallFolder))
      return PORT_INSTALL_CLASSIC;
    if (PortCascCheck(o->szInstallFolder, szInstall, sizeof szInstall))
      return PORT_INSTALL_BATTLENET;
  }
  return PORT_INSTALL_UNSET;
}

PortInstallKind PortInstallCommandLine(void)
{
  static const PortInstallKind s_kind = CommandLineKind();
  return s_kind;
}

// The kind this run reads: the command line's, else settings.json's.
static PortInstallKind RunKind(void)
{
  const PortInstallKind kind = PortInstallCommandLine();
  return kind != PORT_INSTALL_UNSET ? kind : PortInstallChosen();
}

void PortInstallChoose(PortInstallKind kind, const char *pszFolder)
{
  if (kind == PORT_INSTALL_UNSET)
    return;
  PortSettingsHold();
  PortSettingSetString("hyperion", "data", kind == PORT_INSTALL_CLASSIC ? "mpq" : "remastered");
  if (pszFolder != NULL)
    PortSettingSetString("hyperion", kind == PORT_INSTALL_CLASSIC ? "classic" : "remastered", pszFolder);
  PortSettingsRelease();
}

static bool EndsInSeparator(const char *psz)
{
  const size_t n = SDL_strlen(psz);
  return n > 0 && (psz[n - 1] == '\\' || psz[n - 1] == '/');
}

static bool HasFile(const char *pszFolder, const char *pszName)
{
  char szPath[MAX_PATH];
  SDL_snprintf(szPath, sizeof szPath, "%s%s%s", pszFolder, EndsInSeparator(pszFolder) ? "" : "\\", pszName);
  return GetFileAttributesA(szPath) != INVALID_FILE_ATTRIBUTES;
}

bool PortInstallClassicCheck(const char *pszFolder)
{
  return pszFolder != NULL && pszFolder[0] != 0 && HasFile(pszFolder, "StarDat.mpq");
}

bool PortInstallCommandFolder(char *pszOut, size_t cchOut)
{
  const char *pszFolder = PortCommandOptions()->szInstallFolder;
  if (PortInstallCommandLine() != PORT_INSTALL_CLASSIC || !PortInstallClassicCheck(pszFolder))
    return false;
  SDL_snprintf(pszOut, cchOut, "%s%s", pszFolder, EndsInSeparator(pszFolder) ? "" : "\\");
  return true;
}

bool PortInstallClassicFolder(char *pszOut, size_t cchOut)
{
  char szFolder[MAX_PATH] = "";
  if (RunKind() == PORT_INSTALL_BATTLENET ||
      !PortSettingString("hyperion", "classic", szFolder, sizeof szFolder) || !PortInstallClassicCheck(szFolder))
    return false;
  SDL_snprintf(pszOut, cchOut, "%s%s", szFolder, EndsInSeparator(szFolder) ? "" : "\\");
  return true;
}

bool PortInstallFound(const char *pszGameFolder)
{
  const bool bClassic = PortInstallClassicCheck(pszGameFolder);
  switch (RunKind()) {
  case PORT_INSTALL_CLASSIC:
    return bClassic;
  case PORT_INSTALL_BATTLENET:
    return PortCascAvailable() != 0;
  default:
    return bClassic || PortCascAvailable() != 0;
  }
}

bool PortInstallMayAsk(void)
{
#if defined(__EMSCRIPTEN__)
  return false;  // the browser's page gives the game's files (platform.md)
#else
  const PortOptions *o = PortCommandOptions();
  if (o->szReplay[0] || o->szMap[0] || o->szLoad[0] || o->szCanvas[0] || o->bBench || o->szBenchReplay[0] ||
      o->szLabWorker[0] || o->szLanCopy[0] || o->szLanStream[0] || o->szLanTab[0] || o->bRoom ||
      o->szInstall[0] || o->szInstallFolder[0])
    return false;
  // read as Windows has them, not from SDL's copy of the environment, which the start's own
  // HYPERION_GAME does not reach
  char szValue[MAX_PATH];
  if (GetEnvironmentVariableA("HYPERION_DATA_SOURCE", szValue, sizeof szValue) != 0 ||
      GetEnvironmentVariableA("HYPERION_REMASTERED", szValue, sizeof szValue) != 0 ||
      GetEnvironmentVariableA("HYPERION_GAME", szValue, sizeof szValue) != 0)
    return false;
  if (GetEnvironmentVariableA("HYPERION_WINDOW", szValue, sizeof szValue) == 0)
    return true;
  return SDL_strcasecmp(szValue, "hidden") != 0 && SDL_strcasecmp(szValue, "background") != 0;
#endif
}

const char *PortInstallBattleNetFolder(void)
{
#if defined(_WIN32)
  static const char *const kFolders[] = {"C:\\Program Files (x86)\\StarCraft"};
#elif defined(__APPLE__)
  static const char *const kFolders[] = {"/Applications/StarCraft"};
#else
  // Battle.net under Wine: Lutris's prefix, then Wine's own
  static const char *const kFolders[] = {"~/Games/battlenet/drive_c/Program Files (x86)/StarCraft",
                                         "~/.wine/drive_c/Program Files (x86)/StarCraft"};
#endif
  static std::string s_folder;
  for (const char *psz : kFolders) {
    s_folder = psz;
    const char *pszHome = SDL_getenv("HOME");
    if (s_folder[0] == '~') {
      if (pszHome == NULL)
        continue;
      s_folder.replace(0, 1, pszHome);
    }
    if (GetFileAttributesA(s_folder.c_str()) != INVALID_FILE_ATTRIBUTES)
      return s_folder.c_str();
  }
  return NULL;
}

PortInstallCheck PortInstallCheckFolder(PortInstallKind kind, const char *pszFolder)
{
  PortInstallCheck check = {false, "", ""};
  if (pszFolder == NULL || pszFolder[0] == 0)
    return check;
  char szInstall[MAX_PATH];
  const bool bBattleNet = PortCascCheck(pszFolder, szInstall, sizeof szInstall) != 0;
  const bool bClassic = PortInstallClassicCheck(pszFolder);
  if (kind == PORT_INSTALL_BATTLENET && bBattleNet) {
    check.bFound = true;
    check.folder = szInstall;
    char szLocale[16];
    PortCascTextLocaleOf(szInstall, szLocale, sizeof szLocale);
    // {0} the install's folder, {1} the name of the language its text is in
    check.text = PortTextF("Files located: StarCraft: Remastered in {0}, its text in {1}.",
                           {szInstall, PortCascLanguageName(szLocale)});
  } else if (kind == PORT_INSTALL_CLASSIC && bClassic) {
    check.bFound = true;
    check.folder = pszFolder;
    std::string files = "StarDat.mpq";
    if (HasFile(pszFolder, "BrooDat.mpq"))
      files += ", BrooDat.mpq";
    if (HasFile(pszFolder, "patch_rt.mpq"))
      files += ", patch_rt.mpq";
    // {0} the game's files that are there, by name
    check.text = PortTextF("Files located: {0}.", {files});
  } else {
    check.text = PortText("Files not located. Are you sure you chose the correct folder?");
    if (kind == PORT_INSTALL_BATTLENET && bClassic)
      check.text += PortText(" This one holds classic StarCraft's files: put it under Classic.");
    else if (kind == PORT_INSTALL_CLASSIC && bBattleNet)
      check.text += PortText(" This one is StarCraft: Remastered: put it under StarCraft: Remastered.");
  }
  return check;
}

bool PortInstallKeep(PortInstallKind kind, const PortInstallCheck &check, std::string *pWhy)
{
  PortInstallChoose(kind, check.folder.c_str());
  if (kind != PORT_INSTALL_BATTLENET)
    return true;
  PortCascUse(check.folder.c_str());
  if (PortCascAvailable())
    return true;
  *pWhy = PortText("Files located, but Hyperion could not open them. Let the Battle.net app finish installing or "
                   "updating StarCraft, then try again.");
  return false;
}

std::string PortInstallStartFolder(PortInstallKind kind)
{
  char szFolder[MAX_PATH] = "";
  if (kind == PORT_INSTALL_BATTLENET) {
    if (PortSettingString("hyperion", "remastered", szFolder, sizeof szFolder) && szFolder[0] != 0)
      return szFolder;
#if defined(_WIN32)
    // where the Battle.net app put it, as its entry in Windows' installed programs says
    if (PortCascFolder()[0] != 0)
      return PortCascFolder();
#endif
    return PortInstallBattleNetFolder() != NULL ? PortInstallBattleNetFolder() : "";
  }
  if (PortSettingString("hyperion", "classic", szFolder, sizeof szFolder) && szFolder[0] != 0)
    return szFolder;
  // a classic folder Hyperion was put in, beside StarDat.mpq
  return PortInstallClassicCheck(PortProgramFolder()) ? PortProgramFolder() : "";
}

PortInstallKind PortInstallBaseFor(PortInstallKind chosen, bool bBattleNet, bool bClassic)
{
  if (chosen == PORT_INSTALL_CLASSIC)
    return bClassic || !bBattleNet ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET;
  return bBattleNet || !bClassic ? PORT_INSTALL_BATTLENET : PORT_INSTALL_CLASSIC;
}

bool PortInstallKeepBoth(PortInstallKind base, const PortInstallCheck &battleNet, const PortInstallCheck &classic,
                         std::string *pWhy)
{
  const PortInstallCheck &other = base == PORT_INSTALL_BATTLENET ? classic : battleNet;
  if (other.bFound)
    PortSettingSetString("hyperion", base == PORT_INSTALL_BATTLENET ? "classic" : "remastered", other.folder.c_str());
  return PortInstallKeep(base, base == PORT_INSTALL_BATTLENET ? battleNet : classic, pWhy);
}

#if defined(_WIN32)

// On Windows, a window of Windows' own controls (hyperion.rc's IDD_PORT_INSTALL): a part for each
// kind of install with the folder it wants, a field with Browse, and under it whether the game's
// files are there, in dark green or dark red (grey while it is empty); then which is the base, of
// those found. Start is there only when the base's files are.
struct InstallDialog {
  PortInstallKind base;
  PortInstallCheck battleNet, classic;
  bool bBattleNetEmpty, bClassicEmpty;
  HFONT hBold;
};

static const char *const kNotLinked = PT_("Not linked. Leave it empty if you do not have this one.");

static void DialogShow(HWND hDlg, InstallDialog *pDialog)
{
  SetDlgItemTextA(hDlg, IDC_INSTALL_BATTLENET_STATUS,
                  pDialog->bBattleNetEmpty ? PortText(kNotLinked) : pDialog->battleNet.text.c_str());
  SetDlgItemTextA(hDlg, IDC_INSTALL_CLASSIC_STATUS,
                  pDialog->bClassicEmpty ? PortText(kNotLinked) : pDialog->classic.text.c_str());
  // only an install whose files are there can be the base
  EnableWindow(GetDlgItem(hDlg, IDC_INSTALL_BATTLENET), pDialog->battleNet.bFound);
  EnableWindow(GetDlgItem(hDlg, IDC_INSTALL_CLASSIC), pDialog->classic.bFound);
  CheckRadioButton(hDlg, IDC_INSTALL_BATTLENET, IDC_INSTALL_CLASSIC,
                   pDialog->base == PORT_INSTALL_CLASSIC ? IDC_INSTALL_CLASSIC : IDC_INSTALL_BATTLENET);
  const PortInstallCheck &base = pDialog->base == PORT_INSTALL_CLASSIC ? pDialog->classic : pDialog->battleNet;
  EnableWindow(GetDlgItem(hDlg, IDOK), base.bFound);
  InvalidateRect(GetDlgItem(hDlg, IDC_INSTALL_BATTLENET_STATUS), NULL, TRUE);
  InvalidateRect(GetDlgItem(hDlg, IDC_INSTALL_CLASSIC_STATUS), NULL, TRUE);
}

static void DialogCheck(HWND hDlg, InstallDialog *pDialog)
{
  char szFolder[MAX_PATH] = "";
  GetDlgItemTextA(hDlg, IDC_INSTALL_BATTLENET_FOLDER, szFolder, sizeof szFolder);
  pDialog->bBattleNetEmpty = szFolder[0] == 0;
  pDialog->battleNet = PortInstallCheckFolder(PORT_INSTALL_BATTLENET, szFolder);
  szFolder[0] = 0;
  GetDlgItemTextA(hDlg, IDC_INSTALL_CLASSIC_FOLDER, szFolder, sizeof szFolder);
  pDialog->bClassicEmpty = szFolder[0] == 0;
  pDialog->classic = PortInstallCheckFolder(PORT_INSTALL_CLASSIC, szFolder);
  pDialog->base = PortInstallBaseFor(pDialog->base, pDialog->battleNet.bFound, pDialog->classic.bFound);
  DialogShow(hDlg, pDialog);
}

// Browse: Windows' folder dialog, starting at the folder in the field, which it fills.
static void DialogBrowse(HWND hDlg, int nField)
{
  IFileOpenDialog *pPicker = NULL;
  if (FAILED(CoCreateInstance(__uuidof(FileOpenDialog), NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pPicker))))
    return;
  DWORD dwOptions = 0;
  pPicker->GetOptions(&dwOptions);
  pPicker->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
  wchar_t wszStart[MAX_PATH];
  IShellItem *pStart = NULL;
  if (GetDlgItemTextW(hDlg, nField, wszStart, MAX_PATH) > 0 &&
      SUCCEEDED(SHCreateItemFromParsingName(wszStart, NULL, IID_PPV_ARGS(&pStart)))) {
    pPicker->SetFolder(pStart);
    pStart->Release();
  }
  IShellItem *pItem = NULL;
  if (SUCCEEDED(pPicker->Show(hDlg)) && SUCCEEDED(pPicker->GetResult(&pItem))) {
    wchar_t *pwszPath = NULL;
    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pwszPath))) {
      SetDlgItemTextW(hDlg, nField, pwszPath);  // its EN_CHANGE checks it
      CoTaskMemFree(pwszPath);
    }
    pItem->Release();
  }
  pPicker->Release();
}

static INT_PTR CALLBACK InstallDialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  InstallDialog *pDialog = reinterpret_cast<InstallDialog *>(GetWindowLongPtrA(hDlg, DWLP_USER));
  switch (uMsg) {
  case WM_INITDIALOG: {
    pDialog = reinterpret_cast<InstallDialog *>(lParam);
    SetWindowLongPtrA(hDlg, DWLP_USER, lParam);
    HICON hIcon = LoadIconA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(1));
    SendMessageA(hDlg, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
    SendMessageA(hDlg, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    // the parts' names and the lines that say whether the files are there, in the window's own
    // font, bold
    LOGFONTA lf;
    if (GetObjectA(reinterpret_cast<HFONT>(SendMessageA(hDlg, WM_GETFONT, 0, 0)), sizeof lf, &lf) != 0) {
      lf.lfWeight = FW_BOLD;
      pDialog->hBold = CreateFontIndirectA(&lf);
      for (int nId : {IDC_INSTALL_BATTLENET_TITLE, IDC_INSTALL_CLASSIC_TITLE, IDC_INSTALL_BATTLENET_STATUS,
                      IDC_INSTALL_CLASSIC_STATUS})
        SendDlgItemMessageA(hDlg, nId, WM_SETFONT, reinterpret_cast<WPARAM>(pDialog->hBold), FALSE);
    }
    // the fields' own EN_CHANGE checks them as they are filled
    SetDlgItemTextA(hDlg, IDC_INSTALL_BATTLENET_FOLDER, PortInstallStartFolder(PORT_INSTALL_BATTLENET).c_str());
    SetDlgItemTextA(hDlg, IDC_INSTALL_CLASSIC_FOLDER, PortInstallStartFolder(PORT_INSTALL_CLASSIC).c_str());
    DialogCheck(hDlg, pDialog);
    return TRUE;
  }
  case WM_CTLCOLORSTATIC: {
    const HWND hCtl = reinterpret_cast<HWND>(lParam);
    const bool bBattleNet = hCtl == GetDlgItem(hDlg, IDC_INSTALL_BATTLENET_STATUS);
    if (pDialog != NULL && (bBattleNet || hCtl == GetDlgItem(hDlg, IDC_INSTALL_CLASSIC_STATUS))) {
      HDC hdc = reinterpret_cast<HDC>(wParam);
      const bool bEmpty = bBattleNet ? pDialog->bBattleNetEmpty : pDialog->bClassicEmpty;
      const bool bFound = bBattleNet ? pDialog->battleNet.bFound : pDialog->classic.bFound;
      SetTextColor(hdc, bEmpty ? GetSysColor(COLOR_GRAYTEXT) : bFound ? RGB(0, 100, 0) : RGB(139, 0, 0));
      SetBkMode(hdc, TRANSPARENT);
      return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_BTNFACE));
    }
    return FALSE;
  }
  case WM_COMMAND:
    switch (LOWORD(wParam)) {
    case IDC_INSTALL_BATTLENET:
    case IDC_INSTALL_CLASSIC:
      if (HIWORD(wParam) == BN_CLICKED) {
        pDialog->base = LOWORD(wParam) == IDC_INSTALL_CLASSIC ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET;
        DialogCheck(hDlg, pDialog);
      }
      return TRUE;
    case IDC_INSTALL_BATTLENET_FOLDER:
    case IDC_INSTALL_CLASSIC_FOLDER:
      if (HIWORD(wParam) == EN_CHANGE)
        DialogCheck(hDlg, pDialog);
      return TRUE;
    case IDC_INSTALL_BATTLENET_BROWSE:
      DialogBrowse(hDlg, IDC_INSTALL_BATTLENET_FOLDER);
      return TRUE;
    case IDC_INSTALL_CLASSIC_BROWSE:
      DialogBrowse(hDlg, IDC_INSTALL_CLASSIC_FOLDER);
      return TRUE;
    case IDOK: {
      std::string why;
      PortInstallCheck &base = pDialog->base == PORT_INSTALL_CLASSIC ? pDialog->classic : pDialog->battleNet;
      if (!base.bFound)
        return TRUE;
      if (!PortInstallKeepBoth(pDialog->base, pDialog->battleNet, pDialog->classic, &why)) {
        base.bFound = false;
        base.text = why;
        DialogShow(hDlg, pDialog);
        return TRUE;
      }
      EndDialog(hDlg, IDOK);
      return TRUE;
    }
    case IDCANCEL:
      EndDialog(hDlg, IDCANCEL);
      return TRUE;
    }
    break;
  }
  return FALSE;
}

PortInstallAnswer PortInstallFirstStart(void)
{
  // HYPERION_INSTALL_WINDOW=drawn: the window Linux and a Mac have, to try it here
  char szWindow[16];
  if (GetEnvironmentVariableA("HYPERION_INSTALL_WINDOW", szWindow, sizeof szWindow) != 0 &&
      SDL_strcasecmp(szWindow, "drawn") == 0)
    return PortInstallWindowRun();
  // drawn at the screen's own scale, as SDL draws its message boxes
  typedef DPI_AWARENESS_CONTEXT(WINAPI * SetContextFn)(DPI_AWARENESS_CONTEXT);
  SetContextFn pfnSetContext =
      reinterpret_cast<SetContextFn>(GetProcAddress(GetModuleHandleA("user32.dll"), "SetThreadDpiAwarenessContext"));
  DPI_AWARENESS_CONTEXT previous = NULL;
  if (pfnSetContext != NULL)
    previous = pfnSetContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  const HRESULT hrCom = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  // the base as kept, when it is asked again for a base whose files are gone
  InstallDialog dialog = {PortInstallChosen() == PORT_INSTALL_CLASSIC ? PORT_INSTALL_CLASSIC : PORT_INSTALL_BATTLENET,
                          {false, "", ""}, {false, "", ""}, true, true, NULL};
  const INT_PTR nResult = DialogBoxParamA(GetModuleHandleA(NULL), MAKEINTRESOURCEA(IDD_PORT_INSTALL), NULL,
                                          InstallDialogProc, reinterpret_cast<LPARAM>(&dialog));
  if (dialog.hBold != NULL)
    DeleteObject(dialog.hBold);
  if (SUCCEEDED(hrCom))
    CoUninitialize();
  if (pfnSetContext != NULL && previous != NULL)
    pfnSetContext(previous);
  if (nResult == -1)
    return PORT_INSTALL_ANSWER_NO_WINDOW;
  return nResult == IDOK ? PORT_INSTALL_ANSWER_CHOSEN : PORT_INSTALL_ANSWER_QUIT;
}

#else

PortInstallAnswer PortInstallFirstStart(void) { return PortInstallWindowRun(); }

#endif
